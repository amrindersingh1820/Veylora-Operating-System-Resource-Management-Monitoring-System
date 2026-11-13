import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// Typedefs for C ABI functions
typedef _CInit = Int32 Function(Pointer<Utf8> dbPath);
typedef _DartInit = int Function(Pointer<Utf8> dbPath);

typedef _CShutdown = Int32 Function();
typedef _DartShutdown = int Function();

typedef _CIsInit = Int32 Function();
typedef _DartIsInit = int Function();

typedef _CVersion = Pointer<Utf8> Function();
typedef _DartVersion = Pointer<Utf8> Function();

typedef _CBufferOnly = Int32 Function(Pointer<Utf8> outBuf, IntPtr outSz);
typedef _DartBufferOnly = int Function(Pointer<Utf8> outBuf, int outSz);

typedef _CIdAndBuffer = Int32 Function(Pointer<Utf8> id, Pointer<Utf8> outBuf, IntPtr outSz);
typedef _DartIdAndBuffer = int Function(Pointer<Utf8> id, Pointer<Utf8> outBuf, int outSz);

typedef _CIdFilterBuffer = Int32 Function(Pointer<Utf8> id, Pointer<Utf8> filter, Pointer<Utf8> outBuf, IntPtr outSz);
typedef _DartIdFilterBuffer = int Function(Pointer<Utf8> id, Pointer<Utf8> filter, Pointer<Utf8> outBuf, int outSz);

typedef _CIdIntBuffer = Int32 Function(Pointer<Utf8> id, Int32 val, Pointer<Utf8> outBuf, IntPtr outSz);
typedef _DartIdIntBuffer = int Function(Pointer<Utf8> id, int val, Pointer<Utf8> outBuf, int outSz);

typedef _CVmCreate = Int32 Function(Pointer<Utf8> configJson, Pointer<Utf8> outErr, IntPtr errSz);
typedef _DartVmCreate = int Function(Pointer<Utf8> configJson, Pointer<Utf8> outErr, int errSz);

typedef _CAppRegister = Int32 Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> appId,
  Pointer<Utf8> name,
  Pointer<Utf8> execPath,
  Pointer<Utf8> desc,
  Pointer<Utf8> cat,
  Pointer<Utf8> ver,
  Pointer<Utf8> profileId,
  Pointer<Utf8> outErr,
  IntPtr errSz,
);
typedef _DartAppRegister = int Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> appId,
  Pointer<Utf8> name,
  Pointer<Utf8> execPath,
  Pointer<Utf8> desc,
  Pointer<Utf8> cat,
  Pointer<Utf8> ver,
  Pointer<Utf8> profileId,
  Pointer<Utf8> outErr,
  int errSz,
);

typedef _CAppLaunch = Int32 Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> appId,
  Pointer<Utf8> args,
  Pointer<Utf8> profileOverride,
  Pointer<Utf8> outInstId,
  IntPtr idSz,
  Pointer<Utf8> outErr,
  IntPtr errSz,
);
typedef _DartAppLaunch = int Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> appId,
  Pointer<Utf8> args,
  Pointer<Utf8> profileOverride,
  Pointer<Utf8> outInstId,
  int idSz,
  Pointer<Utf8> outErr,
  int errSz,
);

typedef _CAppStop = Int32 Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> instId,
  Int32 timeoutMs,
  Int32 force,
  Pointer<Utf8> outErr,
  IntPtr errSz,
);
typedef _DartAppStop = int Function(
  Pointer<Utf8> vmId,
  Pointer<Utf8> instId,
  int timeoutMs,
  int force,
  Pointer<Utf8> outErr,
  int errSz,
);

typedef _CProfileSave = Int32 Function(
  Pointer<Utf8> profileId,
  Pointer<Utf8> name,
  Pointer<Utf8> desc,
  Double cpuCores,
  Int32 cpuWeight,
  Uint64 memBytes,
  Int64 pidsMax,
  Uint64 ioRbps,
  Uint64 ioWbps,
  Pointer<Utf8> outErr,
  IntPtr errSz,
);
typedef _DartProfileSave = int Function(
  Pointer<Utf8> profileId,
  Pointer<Utf8> name,
  Pointer<Utf8> desc,
  double cpuCores,
  int cpuWeight,
  int memBytes,
  int pidsMax,
  int ioRbps,
  int ioWbps,
  Pointer<Utf8> outErr,
  int errSz,
);

typedef _CSchedSim = Int32 Function(
  Int32 algo,
  Pointer<Utf8> procsJson,
  Int32 quantum,
  Pointer<Utf8> outJson,
  IntPtr outSz,
);
typedef _DartSchedSim = int Function(
  int algo,
  Pointer<Utf8> procsJson,
  int quantum,
  Pointer<Utf8> outJson,
  int outSz,
);

typedef _CMemSim = Int32 Function(
  Int32 strategy,
  Pointer<Utf8> procsJson,
  Pointer<Utf8> blocksJson,
  Pointer<Utf8> outJson,
  IntPtr outSz,
);
typedef _DartMemSim = int Function(
  int strategy,
  Pointer<Utf8> procsJson,
  Pointer<Utf8> blocksJson,
  Pointer<Utf8> outJson,
  int outSz,
);

typedef _CBankerSim = Int32 Function(
  Pointer<Utf8> procsJson,
  Pointer<Utf8> availJson,
  Pointer<Utf8> outJson,
  IntPtr outSz,
);
typedef _DartBankerSim = int Function(
  Pointer<Utf8> procsJson,
  Pointer<Utf8> availJson,
  Pointer<Utf8> outJson,
  int outSz,
);

typedef _CEventLog = Int32 Function(
  Pointer<Utf8> instanceId,
  Pointer<Utf8> appId,
  Pointer<Utf8> vmId,
  Pointer<Utf8> eventType,
  Pointer<Utf8> details,
);
typedef _DartEventLog = int Function(
  Pointer<Utf8> instanceId,
  Pointer<Utf8> appId,
  Pointer<Utf8> vmId,
  Pointer<Utf8> eventType,
  Pointer<Utf8> details,
);

class VosFfiBridge {
  static VosFfiBridge? _instance;
  late DynamicLibrary _lib;

  // Functions
  late _DartInit _init;
  late _DartShutdown _shutdown;
  late _DartIsInit _isInit;
  late _DartVersion _version;
  late _DartBufferOnly _getHostTelemetry;
  late _DartBufferOnly _vmList;
  late _DartIdAndBuffer _vmGet;
  late _DartVmCreate _vmCreate;
  late _DartIdAndBuffer _vmDelete;
  late _DartIdAndBuffer _vmStart;
  late _DartIdIntBuffer _vmStop;
  late _DartIdAndBuffer _vmTelemetry;
  late _DartIdAndBuffer _appDiscover;
  late _DartAppRegister _appRegister;
  late _DartIdFilterBuffer _appUnregister;
  late _DartIdAndBuffer _appList;
  late _DartAppLaunch _appLaunch;
  late _DartAppStop _appStop;
  late _DartIdFilterBuffer _appInstancesList;
  late _DartIdFilterBuffer _appCollectMetrics;
  late _DartBufferOnly _profileList;
  late _DartIdAndBuffer _profileGet;
  late _DartProfileSave _profileSave;
  late _DartIdAndBuffer _profileDelete;
  late _DartSchedSim _simRunScheduler;
  late _DartMemSim _simRunMemory;
  late _DartBankerSim _simRunBankers;
  late _DartIdIntBuffer _eventsList;
  _DartEventLog? _eventLog;

  VosFfiBridge._() {
    _loadLibrary();
    _bindSymbols();
  }

  static VosFfiBridge get instance {
    _instance ??= VosFfiBridge._();
    return _instance!;
  }

  void _loadLibrary() {
    final exeDir = File(Platform.resolvedExecutable).parent.path;
    final frameworksPath = '$exeDir/../Frameworks/libvos_backend.dylib';

    final possiblePaths = [
      frameworksPath,
      '$exeDir/libvos_backend.dylib',
      '/Users/amrindersingh/Downloads/VirtualOS_Simulator/build/libvos_backend.dylib',
      'build/libvos_backend.dylib',
      '../build/libvos_backend.dylib',
      '../../build/libvos_backend.dylib',
      'libvos_backend.dylib',
    ];

    DynamicLibrary? loaded;
    final errors = <String>[];

    for (final p in possiblePaths) {
      final f = File(p);
      if (f.existsSync()) {
        try {
          loaded = DynamicLibrary.open(f.path);
          break;
        } catch (e) {
          errors.add('$p (exists but failed open: $e)');
        }
      }
    }

    if (loaded == null) {
      try {
        loaded = DynamicLibrary.open('libvos_backend.dylib');
      } catch (e) {
        errors.add('system dlopen: $e');
      }
    }

    if (loaded == null) {
      throw Exception(
        'Failed to load libvos_backend.dylib.\n'
        'Looked in:\n${possiblePaths.map((p) => " - $p").join("\n")}\n'
        'Errors encountered:\n${errors.join("\n")}\n'
        'Please run "make dylib" in the project root to generate build/libvos_backend.dylib.',
      );
    }

    _lib = loaded;
  }

  void _bindSymbols() {
    _init = _lib.lookupFunction<_CInit, _DartInit>('vos_api_init');
    _shutdown = _lib.lookupFunction<_CShutdown, _DartShutdown>('vos_api_shutdown');
    _isInit = _lib.lookupFunction<_CIsInit, _DartIsInit>('vos_api_is_initialized');
    _version = _lib.lookupFunction<_CVersion, _DartVersion>('vos_api_version');
    _getHostTelemetry = _lib.lookupFunction<_CBufferOnly, _DartBufferOnly>('vos_api_get_host_telemetry');
    _vmList = _lib.lookupFunction<_CBufferOnly, _DartBufferOnly>('vos_api_vm_list');
    _vmGet = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_vm_get');
    _vmCreate = _lib.lookupFunction<_CVmCreate, _DartVmCreate>('vos_api_vm_create');
    _vmDelete = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_vm_delete');
    _vmStart = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_vm_start');
    _vmStop = _lib.lookupFunction<_CIdIntBuffer, _DartIdIntBuffer>('vos_api_vm_stop');
    _vmTelemetry = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_vm_telemetry');
    _appDiscover = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_app_discover');
    _appRegister = _lib.lookupFunction<_CAppRegister, _DartAppRegister>('vos_api_app_register');
    _appUnregister = _lib.lookupFunction<_CIdFilterBuffer, _DartIdFilterBuffer>('vos_api_app_unregister');
    _appList = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_app_list');
    _appLaunch = _lib.lookupFunction<_CAppLaunch, _DartAppLaunch>('vos_api_app_launch');
    _appStop = _lib.lookupFunction<_CAppStop, _DartAppStop>('vos_api_app_stop');
    _appInstancesList = _lib.lookupFunction<_CIdFilterBuffer, _DartIdFilterBuffer>('vos_api_app_instances_list');
    _appCollectMetrics = _lib.lookupFunction<_CIdFilterBuffer, _DartIdFilterBuffer>('vos_api_app_collect_metrics');
    _profileList = _lib.lookupFunction<_CBufferOnly, _DartBufferOnly>('vos_api_profile_list');
    _profileGet = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_profile_get');
    _profileSave = _lib.lookupFunction<_CProfileSave, _DartProfileSave>('vos_api_profile_save');
    _profileDelete = _lib.lookupFunction<_CIdAndBuffer, _DartIdAndBuffer>('vos_api_profile_delete');
    _simRunScheduler = _lib.lookupFunction<_CSchedSim, _DartSchedSim>('vos_api_sim_run_scheduler');
    _simRunMemory = _lib.lookupFunction<_CMemSim, _DartMemSim>('vos_api_sim_run_memory');
    _simRunBankers = _lib.lookupFunction<_CBankerSim, _DartBankerSim>('vos_api_sim_run_bankers');
    _eventsList = _lib.lookupFunction<_CIdIntBuffer, _DartIdIntBuffer>('vos_api_events_list');
    try {
      _eventLog = _lib.lookupFunction<_CEventLog, _DartEventLog>('vos_api_event_log');
    } catch (_) {
      _eventLog = null;
    }
  }

  // --- High-Level Dart API Wrappers ---

  int init(String dbPath) {
    final pathPtr = dbPath.toNativeUtf8();
    try {
      return _init(pathPtr);
    } finally {
      calloc.free(pathPtr);
    }
  }

  int shutdown() => _shutdown();
  bool get isInitialized => _isInit() == 1;
  String get version => _version().toDartString();

  Map<String, dynamic> getHostTelemetry() {
    final buf = calloc<Uint8>(4096).cast<Utf8>();
    try {
      final res = _getHostTelemetry(buf, 4096);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(buf);
    }
  }

  List<dynamic> listVms() {
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _vmList(buf, 16384);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? [] : jsonDecode(jsonStr) as List<dynamic>;
    } catch (_) {
      return [];
    } finally {
      calloc.free(buf);
    }
  }

  Map<String, dynamic> getVm(String vmId) {
    final idPtr = vmId.toNativeUtf8();
    final buf = calloc<Uint8>(4096).cast<Utf8>();
    try {
      final res = _vmGet(idPtr, buf, 4096);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(idPtr);
      calloc.free(buf);
    }
  }

  String? createVm(Map<String, dynamic> config) {
    final jsonPtr = jsonEncode(config).toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _vmCreate(jsonPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'VM creation failed';
      }
      return null;
    } finally {
      calloc.free(jsonPtr);
      calloc.free(errBuf);
    }
  }

  String? deleteVm(String vmId) {
    final idPtr = vmId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _vmDelete(idPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'VM deletion failed';
      }
      return null;
    } finally {
      calloc.free(idPtr);
      calloc.free(errBuf);
    }
  }

  String? startVm(String vmId) {
    final idPtr = vmId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _vmStart(idPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'VM start failed';
      }
      return null;
    } finally {
      calloc.free(idPtr);
      calloc.free(errBuf);
    }
  }

  String? stopVm(String vmId, {bool force = false}) {
    final idPtr = vmId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _vmStop(idPtr, force ? 1 : 0, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'VM stop failed';
      }
      return null;
    } finally {
      calloc.free(idPtr);
      calloc.free(errBuf);
    }
  }

  Map<String, dynamic> getVmTelemetry(String vmId) {
    final idPtr = vmId.toNativeUtf8();
    final buf = calloc<Uint8>(4096).cast<Utf8>();
    try {
      final res = _vmTelemetry(idPtr, buf, 4096);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(idPtr);
      calloc.free(buf);
    }
  }

  List<dynamic> discoverApps(String vmId) {
    final idPtr = vmId.toNativeUtf8();
    final buf = calloc<Uint8>(32768).cast<Utf8>();
    try {
      final res = _appDiscover(idPtr, buf, 32768);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      if (jsonStr.isEmpty) return [];
      final parsed = jsonDecode(jsonStr) as Map<String, dynamic>;
      return (parsed['apps'] as List<dynamic>?) ?? [];
    } catch (_) {
      return [];
    } finally {
      calloc.free(idPtr);
      calloc.free(buf);
    }
  }

  String? registerApp({
    required String vmId,
    required String appId,
    required String name,
    required String executablePath,
    String description = '',
    String category = 'Utility',
    String version = '1.0',
    String defaultProfileId = 'default',
  }) {
    final vPtr = vmId.toNativeUtf8();
    final aPtr = appId.toNativeUtf8();
    final nPtr = name.toNativeUtf8();
    final ePtr = executablePath.toNativeUtf8();
    final dPtr = description.toNativeUtf8();
    final cPtr = category.toNativeUtf8();
    final verPtr = version.toNativeUtf8();
    final pPtr = defaultProfileId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();

    try {
      final res = _appRegister(vPtr, aPtr, nPtr, ePtr, dPtr, cPtr, verPtr, pPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'App registration failed';
      }
      return null;
    } finally {
      calloc.free(vPtr);
      calloc.free(aPtr);
      calloc.free(nPtr);
      calloc.free(ePtr);
      calloc.free(dPtr);
      calloc.free(cPtr);
      calloc.free(verPtr);
      calloc.free(pPtr);
      calloc.free(errBuf);
    }
  }

  String? unregisterApp(String vmId, String appId) {
    final vPtr = vmId.toNativeUtf8();
    final aPtr = appId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _appUnregister(vPtr, aPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'App unregister failed';
      }
      return null;
    } finally {
      calloc.free(vPtr);
      calloc.free(aPtr);
      calloc.free(errBuf);
    }
  }

  List<dynamic> listApps(String vmId) {
    final vPtr = vmId.toNativeUtf8();
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _appList(vPtr, buf, 16384);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? [] : jsonDecode(jsonStr) as List<dynamic>;
    } catch (_) {
      return [];
    } finally {
      calloc.free(vPtr);
      calloc.free(buf);
    }
  }

  Map<String, dynamic> launchApp({
    required String vmId,
    required String appId,
    String args = '',
    String? profileOverride,
  }) {
    final vPtr = vmId.toNativeUtf8();
    final aPtr = appId.toNativeUtf8();
    final argPtr = args.toNativeUtf8();
    final profPtr = (profileOverride ?? '').toNativeUtf8();
    final instBuf = calloc<Uint8>(128).cast<Utf8>();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();

    try {
      final res = _appLaunch(vPtr, aPtr, argPtr, profPtr, instBuf, 128, errBuf, 512);
      if (res != 0) {
        return {'success': false, 'error': errBuf.toDartString()};
      }
      return {'success': true, 'instance_id': instBuf.toDartString()};
    } finally {
      calloc.free(vPtr);
      calloc.free(aPtr);
      calloc.free(argPtr);
      calloc.free(profPtr);
      calloc.free(instBuf);
      calloc.free(errBuf);
    }
  }

  String? stopApp({
    required String vmId,
    required String instanceId,
    int timeoutMs = 3000,
    bool force = false,
  }) {
    final vPtr = vmId.toNativeUtf8();
    final iPtr = instanceId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _appStop(vPtr, iPtr, timeoutMs, force ? 1 : 0, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'App stop failed';
      }
      return null;
    } finally {
      calloc.free(vPtr);
      calloc.free(iPtr);
      calloc.free(errBuf);
    }
  }

  List<dynamic> listAppInstances(String vmId, {String? stateFilter}) {
    final vPtr = vmId.toNativeUtf8();
    final sPtr = (stateFilter ?? '').toNativeUtf8();
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _appInstancesList(vPtr, sPtr, buf, 16384);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? [] : jsonDecode(jsonStr) as List<dynamic>;
    } catch (_) {
      return [];
    } finally {
      calloc.free(vPtr);
      calloc.free(sPtr);
      calloc.free(buf);
    }
  }

  Map<String, dynamic> collectAppMetrics(String vmId, String instanceId) {
    final vPtr = vmId.toNativeUtf8();
    final iPtr = instanceId.toNativeUtf8();
    final buf = calloc<Uint8>(4096).cast<Utf8>();
    try {
      final res = _appCollectMetrics(vPtr, iPtr, buf, 4096);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(vPtr);
      calloc.free(iPtr);
      calloc.free(buf);
    }
  }

  List<dynamic> listProfiles() {
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _profileList(buf, 16384);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? [] : jsonDecode(jsonStr) as List<dynamic>;
    } catch (_) {
      return [];
    } finally {
      calloc.free(buf);
    }
  }

  Map<String, dynamic> getProfile(String profileId) {
    final pPtr = profileId.toNativeUtf8();
    final buf = calloc<Uint8>(4096).cast<Utf8>();
    try {
      final res = _profileGet(pPtr, buf, 4096);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(pPtr);
      calloc.free(buf);
    }
  }

  String? saveProfile({
    required String profileId,
    required String name,
    required String description,
    required double cpuCores,
    required int cpuWeight,
    required int memoryMaxBytes,
    required int pidsMax,
    required int ioReadBps,
    required int ioWriteBps,
  }) {
    final pPtr = profileId.toNativeUtf8();
    final nPtr = name.toNativeUtf8();
    final dPtr = description.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();

    try {
      final res = _profileSave(
        pPtr,
        nPtr,
        dPtr,
        cpuCores,
        cpuWeight,
        memoryMaxBytes,
        pidsMax,
        ioReadBps,
        ioWriteBps,
        errBuf,
        512,
      );
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'Profile save failed';
      }
      return null;
    } finally {
      calloc.free(pPtr);
      calloc.free(nPtr);
      calloc.free(dPtr);
      calloc.free(errBuf);
    }
  }

  String? deleteProfile(String profileId) {
    final pPtr = profileId.toNativeUtf8();
    final errBuf = calloc<Uint8>(512).cast<Utf8>();
    try {
      final res = _profileDelete(pPtr, errBuf, 512);
      if (res != 0) {
        final err = errBuf.toDartString();
        return err.isNotEmpty ? err : 'Profile deletion failed';
      }
      return null;
    } finally {
      calloc.free(pPtr);
      calloc.free(errBuf);
    }
  }

  Map<String, dynamic> runSchedulerSimulation({
    required int algo,
    required List<Map<String, dynamic>> processes,
    int quantum = 0,
  }) {
    final procsJson = jsonEncode(processes).toNativeUtf8();
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _simRunScheduler(algo, procsJson, quantum, buf, 16384);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(procsJson);
      calloc.free(buf);
    }
  }

  Map<String, dynamic> runMemorySimulation({
    required int strategy,
    required List<Map<String, dynamic>> processes,
    required String blocksText,
  }) {
    final procsJson = jsonEncode(processes).toNativeUtf8();
    final blocksPtr = blocksText.toNativeUtf8();
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _simRunMemory(strategy, procsJson, blocksPtr, buf, 16384);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(procsJson);
      calloc.free(blocksPtr);
      calloc.free(buf);
    }
  }

  Map<String, dynamic> runBankersSimulation({
    required List<Map<String, dynamic>> processes,
    required String availableText,
  }) {
    final procsJson = jsonEncode(processes).toNativeUtf8();
    final availPtr = availableText.toNativeUtf8();
    final buf = calloc<Uint8>(16384).cast<Utf8>();
    try {
      final res = _simRunBankers(procsJson, availPtr, buf, 16384);
      if (res != 0) return {};
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? {} : jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    } finally {
      calloc.free(procsJson);
      calloc.free(availPtr);
      calloc.free(buf);
    }
  }

  List<dynamic> listEvents({String? vmId, int limit = 100}) {
    final vPtr = (vmId ?? '').toNativeUtf8();
    final buf = calloc<Uint8>(32768).cast<Utf8>();
    try {
      final res = _eventsList(vPtr, limit, buf, 32768);
      if (res != 0) return [];
      final jsonStr = buf.toDartString();
      return jsonStr.isEmpty ? [] : jsonDecode(jsonStr) as List<dynamic>;
    } catch (_) {
      return [];
    } finally {
      calloc.free(vPtr);
      calloc.free(buf);
    }
  }

  bool logEvent({
    String? instanceId,
    String? appId,
    String? vmId,
    required String eventType,
    required String details,
  }) {
    if (_eventLog == null) return false;
    final instPtr = (instanceId ?? '').toNativeUtf8();
    final appPtr = (appId ?? '').toNativeUtf8();
    final vmPtr = (vmId ?? '').toNativeUtf8();
    final typePtr = eventType.toNativeUtf8();
    final detPtr = details.toNativeUtf8();
    try {
      final res = _eventLog!(instPtr, appPtr, vmPtr, typePtr, detPtr);
      return res == 0;
    } catch (_) {
      return false;
    } finally {
      calloc.free(instPtr);
      calloc.free(appPtr);
      calloc.free(vmPtr);
      calloc.free(typePtr);
      calloc.free(detPtr);
    }
  }
}
