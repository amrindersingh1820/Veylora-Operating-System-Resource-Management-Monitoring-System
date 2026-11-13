import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import '../core/constants.dart';
import '../data/ffi/vos_ffi_bridge.dart';
import '../data/models/host_telemetry.dart';
import '../data/models/vm_model.dart';
import '../data/models/app_model.dart';
import '../data/models/profile_model.dart';
import '../data/models/simulation_models.dart';
import '../data/models/event_model.dart';

class VosService extends ChangeNotifier {
  final VosFfiBridge _bridge = VosFfiBridge.instance;

  bool _initialized = false;
  String _statusMessage = 'Connecting...';
  String? _lastError;

  // Real-time State
  ThemeMode _themeMode = ThemeMode.dark;
  HostTelemetry _hostTelemetry = HostTelemetry.empty();
  List<VmModel> _vms = [];
  String? _selectedVmId;
  List<ResourceProfile> _profiles = [];
  List<RegisteredApp> _registeredApps = [];
  List<AppInstance> _appInstances = [];
  List<DiscoveredApp> _discoveredApps = [];
  List<EventLogEntry> _events = [];

  // Historical samples for charts (ring buffer of last 60 samples, 1 sample/sec)
  final List<double> _hostCpuHistory = [];
  final List<double> _hostRamHistory = [];
  final List<double> _qemuCpuHistory = [];
  final List<double> _qemuRssHistory = [];
  final List<double> _diskReadHistory = [];
  final List<double> _diskWriteHistory = [];
  final List<double> _netRxHistory = [];
  final List<double> _netTxHistory = [];

  // Simulation State
  SchedResultModel? _lastSchedResult;
  MemResultModel? _lastMemResult;
  BankerResultModel? _lastBankerResult;

  Timer? _telemetryTimer;

  // Getters
  bool get isInitialized => _initialized;
  ThemeMode get themeMode => _themeMode;
  String get statusMessage => _statusMessage;
  String? get lastError => _lastError;
  HostTelemetry get hostTelemetry => _hostTelemetry;
  List<VmModel> get vms => _vms;
  String? get selectedVmId => _selectedVmId;
  List<ResourceProfile> get profiles => _profiles;
  List<RegisteredApp> get registeredApps => _registeredApps;
  List<AppInstance> get appInstances => _appInstances;
  List<DiscoveredApp> get discoveredApps => _discoveredApps;
  List<EventLogEntry> get events => _events;

  List<double> get hostCpuHistory => List.unmodifiable(_hostCpuHistory);
  List<double> get hostRamHistory => List.unmodifiable(_hostRamHistory);
  List<double> get qemuCpuHistory => List.unmodifiable(_qemuCpuHistory);
  List<double> get qemuRssHistory => List.unmodifiable(_qemuRssHistory);
  List<double> get diskReadHistory => List.unmodifiable(_diskReadHistory);
  List<double> get diskWriteHistory => List.unmodifiable(_diskWriteHistory);
  List<double> get netRxHistory => List.unmodifiable(_netRxHistory);
  List<double> get netTxHistory => List.unmodifiable(_netTxHistory);

  void toggleTheme() {
    _themeMode = _themeMode == ThemeMode.dark ? ThemeMode.light : ThemeMode.dark;
    notifyListeners();
  }

  void setThemeMode(ThemeMode mode) {
    _themeMode = mode;
    notifyListeners();
  }

  SchedResultModel? get lastSchedResult => _lastSchedResult;
  MemResultModel? get lastMemResult => _lastMemResult;
  BankerResultModel? get lastBankerResult => _lastBankerResult;

  VmModel? get selectedVm {
    if (_selectedVmId == null || _vms.isEmpty) return null;
    return _vms.firstWhere(
      (v) => v.vmId == _selectedVmId,
      orElse: () => _vms.first,
    );
  }

  void selectVm(String? vmId) {
    _selectedVmId = vmId;
    refreshVmSpecificData();
    notifyListeners();
  }

  Future<void> initBackend({String dbPath = VosConstants.defaultDbPath}) async {
    try {
      final res = _bridge.init(dbPath);
      if (res == 0) {
        _initialized = true;
        _statusMessage = 'Engine Online (${_bridge.version})';
        await refreshAll();
        _startTelemetryLoop();
      } else {
        _statusMessage = 'Initialization failed ($res)';
        _lastError = 'Engine returned error code $res';
      }
    } catch (e) {
      _statusMessage = 'Bridge Error';
      _lastError = e.toString();
    }
    notifyListeners();
  }

  void _startTelemetryLoop() {
    _telemetryTimer?.cancel();
    _telemetryTimer = Timer.periodic(VosConstants.telemetryInterval, (_) {
      _sampleTelemetry();
    });
  }

  void _sampleTelemetry() {
    if (!_initialized) return;

    // 1. Host Telemetry
    final hostRaw = _bridge.getHostTelemetry();
    if (hostRaw.isNotEmpty) {
      _hostTelemetry = HostTelemetry.fromJson(hostRaw);
      _hostCpuHistory.add(_hostTelemetry.cpuUsagePct);
      if (_hostCpuHistory.length > 60) _hostCpuHistory.removeAt(0);

      final ramUsedMb = _hostTelemetry.usedRamBytes / (1024 * 1024);
      _hostRamHistory.add(ramUsedMb);
      if (_hostRamHistory.length > 60) _hostRamHistory.removeAt(0);
    }

    // 2. Refresh active VM telemetry
    final curVm = selectedVm;
    if (curVm != null) {
      final telem = _bridge.getVmTelemetry(curVm.vmId);
      if (telem.isNotEmpty) {
        final qemuCpu = (telem['qemu_cpu_pct'] as num?)?.toDouble() ?? 0.0;
        final qemuRss = (telem['qemu_rss_bytes'] as num?)?.toInt() ?? 0;
        final guestReady = (telem['guest_ready'] as num?)?.toInt() ?? 0;
        final freshness = telem['freshness'] as String? ?? 'UNAVAILABLE';
        final state = telem['state'] as String? ?? curVm.state;
        final pid = (telem['qemu_pid'] as num?)?.toInt() ?? curVm.pid;

        _qemuCpuHistory.add(qemuCpu);
        if (_qemuCpuHistory.length > 60) _qemuCpuHistory.removeAt(0);

        final rssMb = qemuRss / (1024 * 1024);
        _qemuRssHistory.add(rssMb);
        if (_qemuRssHistory.length > 60) _qemuRssHistory.removeAt(0);

        final diskR = (telem['disk_read_kbps'] as num?)?.toDouble() ?? 0.0;
        final diskW = (telem['disk_write_kbps'] as num?)?.toDouble() ?? 0.0;
        _diskReadHistory.add(diskR);
        if (_diskReadHistory.length > 60) _diskReadHistory.removeAt(0);
        _diskWriteHistory.add(diskW);
        if (_diskWriteHistory.length > 60) _diskWriteHistory.removeAt(0);

        final netR = (telem['net_rx_kbps'] as num?)?.toDouble() ?? 0.0;
        final netT = (telem['net_tx_kbps'] as num?)?.toDouble() ?? 0.0;
        _netRxHistory.add(netR);
        if (_netRxHistory.length > 60) _netRxHistory.removeAt(0);
        _netTxHistory.add(netT);
        if (_netTxHistory.length > 60) _netTxHistory.removeAt(0);

        final idx = _vms.indexWhere((v) => v.vmId == curVm.vmId);
        if (idx >= 0) {
          _vms[idx] = _vms[idx].copyWithTelemetry(
            qemuCpuPct: qemuCpu,
            qemuRssBytes: qemuRss,
            guestReady: guestReady,
            freshness: freshness,
            state: state,
            pid: pid,
          );
        }
      }
    }

    notifyListeners();
  }

  Future<void> refreshAll() async {
    await refreshVms();
    await refreshProfiles();
    await refreshVmSpecificData();
    await refreshEvents();
  }

  Future<void> refreshVms() async {
    if (!_initialized) return;
    final list = _bridge.listVms();
    _vms = list.map((item) => VmModel.fromJson(item as Map<String, dynamic>)).toList();
    if (_vms.isNotEmpty && (_selectedVmId == null || !_vms.any((v) => v.vmId == _selectedVmId))) {
      _selectedVmId = _vms.first.vmId;
    }
    notifyListeners();
  }

  Future<void> refreshProfiles() async {
    if (!_initialized) return;
    final list = _bridge.listProfiles();
    _profiles = list.map((item) => ResourceProfile.fromJson(item as Map<String, dynamic>)).toList();
    notifyListeners();
  }

  Future<void> refreshVmSpecificData() async {
    if (!_initialized) return;
    if (_selectedVmId == null) {
      _registeredApps = [];
      _appInstances = [];
      return;
    }

    // List Registered Apps
    final appsRaw = _bridge.listApps(_selectedVmId!);
    _registeredApps = appsRaw.map((a) => RegisteredApp.fromJson(a as Map<String, dynamic>)).toList();

    // List App Instances
    final instRaw = _bridge.listAppInstances(_selectedVmId!);
    _appInstances = instRaw.map((i) => AppInstance.fromJson(i as Map<String, dynamic>)).toList();

    notifyListeners();
  }

  Future<void> refreshEvents() async {
    if (!_initialized) return;
    final evs = _bridge.listEvents(vmId: _selectedVmId, limit: 100);
    _events = evs.map((e) => EventLogEntry.fromJson(e as Map<String, dynamic>)).toList();
    notifyListeners();
  }

  Future<bool> logEvent({
    String? instanceId,
    String? appId,
    String? vmId,
    required String eventType,
    required String details,
  }) async {
    final effectiveVmId = (vmId != null && vmId.isNotEmpty) ? vmId : (_selectedVmId ?? 'host_native');
    final effectiveAppId = appId ?? '';
    final effectiveInstId = instanceId ?? 'test_${DateTime.now().millisecondsSinceEpoch}';

    final ok = _bridge.logEvent(
      instanceId: effectiveInstId,
      appId: effectiveAppId,
      vmId: effectiveVmId,
      eventType: eventType,
      details: details,
    );

    await refreshEvents();
    return ok;
  }

  // --- VM Operations ---

  Future<bool> createVm({
    required String vmId,
    required String name,
    required int vcpuCount,
    required int ramMb,
    String diskPath = 'vm_images/alpine.qcow2',
    String isoPath = 'vm_images/alpine-arm64.iso',
  }) async {
    final err = _bridge.createVm({
      'id': vmId,
      'name': name,
      'vcpu_count': vcpuCount,
      'ram_mb': ramMb,
      'disk_path': diskPath,
      'iso_path': isoPath,
    });
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVms();
    _selectedVmId = vmId;
    await refreshVmSpecificData();
    return true;
  }

  Future<bool> deleteVm(String vmId) async {
    final err = _bridge.deleteVm(vmId);
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVms();
    return true;
  }

  Future<bool> startVm(String vmId) async {
    final err = _bridge.startVm(vmId);
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVms();
    return true;
  }

  Future<bool> stopVm(String vmId, {bool force = false}) async {
    final err = _bridge.stopVm(vmId, force: force);
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVms();
    return true;
  }

  Future<String> getVmConsoleLog(String vmId) async {
    final candidates = [
      '${vmId}_console.log',
      '../${vmId}_console.log',
      'frontend/${vmId}_console.log',
      '/Users/amrindersingh/Downloads/VirtualOS_Simulator/${vmId}_console.log',
      '/tmp/${vmId}_console.log',
    ];
    for (final p in candidates) {
      final f = File(p);
      if (f.existsSync()) {
        try {
          final content = await f.readAsString();
          if (content.isNotEmpty) return content;
        } catch (_) {}
      }
    }
    return 'Waiting for guest boot log stream...';
  }

  // --- Application Operations ---

  Future<void> discoverApps(String vmId) async {
    final apps = _bridge.discoverApps(vmId);
    _discoveredApps = apps.map((a) => DiscoveredApp.fromJson(a as Map<String, dynamic>)).toList();
    notifyListeners();
  }

  Future<bool> registerApp({
    required String vmId,
    required String appId,
    required String name,
    required String executablePath,
    String description = '',
    String category = 'Utility',
    String version = '1.0',
    String defaultProfileId = 'default',
  }) async {
    final err = _bridge.registerApp(
      vmId: vmId,
      appId: appId,
      name: name,
      executablePath: executablePath,
      description: description,
      category: category,
      version: version,
      defaultProfileId: defaultProfileId,
    );
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVmSpecificData();
    return true;
  }

  Future<bool> unregisterApp(String vmId, String appId) async {
    final err = _bridge.unregisterApp(vmId, appId);
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVmSpecificData();
    return true;
  }

  Future<String?> launchApp({
    required String vmId,
    required String appId,
    String args = '',
    String? profileOverride,
  }) async {
    final res = _bridge.launchApp(
      vmId: vmId,
      appId: appId,
      args: args,
      profileOverride: profileOverride,
    );
    if (res['success'] != true) {
      _lastError = res['error'] as String? ?? 'Launch failed';
      notifyListeners();
      return null;
    }
    await refreshVmSpecificData();
    await refreshEvents();
    return res['instance_id'] as String?;
  }

  Future<bool> stopApp({
    required String vmId,
    required String instanceId,
    int timeoutMs = 3000,
    bool force = false,
  }) async {
    final err = _bridge.stopApp(
      vmId: vmId,
      instanceId: instanceId,
      timeoutMs: timeoutMs,
      force: force,
    );
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshVmSpecificData();
    await refreshEvents();
    return true;
  }

  // --- Profile Operations ---

  Future<bool> saveProfile({
    required String profileId,
    required String name,
    required String description,
    required double cpuCores,
    required int cpuWeight,
    required int memoryMaxBytes,
    required int pidsMax,
    required int ioReadBps,
    required int ioWriteBps,
  }) async {
    final err = _bridge.saveProfile(
      profileId: profileId,
      name: name,
      description: description,
      cpuCores: cpuCores,
      cpuWeight: cpuWeight,
      memoryMaxBytes: memoryMaxBytes,
      pidsMax: pidsMax,
      ioReadBps: ioReadBps,
      ioWriteBps: ioWriteBps,
    );
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshProfiles();
    return true;
  }

  Future<bool> deleteProfile(String profileId) async {
    final err = _bridge.deleteProfile(profileId);
    if (err != null) {
      _lastError = err;
      notifyListeners();
      return false;
    }
    await refreshProfiles();
    return true;
  }

  // --- Simulation Operations ---

  void runSchedulerSimulation({
    required int algo,
    required List<SimProcess> processes,
    int quantum = 0,
  }) {
    final procsJson = processes.map((p) => p.toJson()).toList();
    final res = _bridge.runSchedulerSimulation(
      algo: algo,
      processes: procsJson,
      quantum: quantum,
    );
    if (res.isNotEmpty && !res.containsKey('error')) {
      _lastSchedResult = SchedResultModel.fromJson(res);
    }
    notifyListeners();
  }

  void runMemorySimulation({
    required int strategy,
    required List<SimProcess> processes,
    required String blocksText,
  }) {
    final procsJson = processes.map((p) => p.toJson()).toList();
    final res = _bridge.runMemorySimulation(
      strategy: strategy,
      processes: procsJson,
      blocksText: blocksText,
    );
    if (res.isNotEmpty && !res.containsKey('error')) {
      _lastMemResult = MemResultModel.fromJson(res);
    }
    notifyListeners();
  }

  void runBankersSimulation({
    required List<SimProcess> processes,
    required String availableText,
  }) {
    final procsJson = processes.map((p) => p.toJson()).toList();
    final res = _bridge.runBankersSimulation(
      processes: procsJson,
      availableText: availableText,
    );
    if (res.isNotEmpty && !res.containsKey('error')) {
      _lastBankerResult = BankerResultModel.fromJson(res);
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _telemetryTimer?.cancel();
    _bridge.shutdown();
    super.dispose();
  }
}

