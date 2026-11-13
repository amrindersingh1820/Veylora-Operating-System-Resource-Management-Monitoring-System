class VmModel {
  final int id;
  final String vmId;
  final String name;
  final int vcpuCount;
  final int ramMb;
  final String state;
  final String osType;
  final String diskPath;
  final String isoPath;
  final int pid;
  final int createdAt;

  // Live telemetry overlay (if sampled)
  final double qemuCpuPct;
  final int qemuRssBytes;
  final int guestReady;
  final String freshness;

  VmModel({
    required this.id,
    required this.vmId,
    required this.name,
    required this.vcpuCount,
    required this.ramMb,
    required this.state,
    required this.osType,
    required this.diskPath,
    required this.isoPath,
    required this.pid,
    this.createdAt = 0,
    this.qemuCpuPct = 0.0,
    this.qemuRssBytes = 0,
    this.guestReady = 0,
    this.freshness = 'UNAVAILABLE',
  });

  bool get isRunning => state == 'RUNNING' || pid > 0;
  bool get isStarting => state == 'STARTING';
  bool get isStopped => state == 'STOPPED' || state == 'CREATED';
  bool get isFailed => state == 'FAILED';
  String get guestArch => 'arm64';

  factory VmModel.fromJson(Map<String, dynamic> json) {
    return VmModel(
      id: (json['id'] as num?)?.toInt() ?? 0,
      vmId: json['vm_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      vcpuCount: (json['vcpu_count'] as num?)?.toInt() ?? 2,
      ramMb: (json['ram_mb'] as num?)?.toInt() ?? 1024,
      state: json['state'] as String? ?? 'STOPPED',
      osType: json['os_type'] as String? ?? 'linux',
      diskPath: json['disk_path'] as String? ?? '',
      isoPath: json['iso_path'] as String? ?? '',
      pid: (json['pid'] as num?)?.toInt() ?? 0,
      createdAt: (json['created_at'] as num?)?.toInt() ?? 0,
      qemuCpuPct: (json['qemu_cpu_pct'] as num?)?.toDouble() ?? 0.0,
      qemuRssBytes: (json['qemu_rss_bytes'] as num?)?.toInt() ?? 0,
      guestReady: (json['guest_ready'] as num?)?.toInt() ?? 0,
      freshness: json['freshness'] as String? ?? 'UNAVAILABLE',
    );
  }

  VmModel copyWithTelemetry({
    double? qemuCpuPct,
    int? qemuRssBytes,
    int? guestReady,
    String? freshness,
    String? state,
    int? pid,
  }) {
    return VmModel(
      id: id,
      vmId: vmId,
      name: name,
      vcpuCount: vcpuCount,
      ramMb: ramMb,
      state: state ?? this.state,
      osType: osType,
      diskPath: diskPath,
      isoPath: isoPath,
      pid: pid ?? this.pid,
      createdAt: createdAt,
      qemuCpuPct: qemuCpuPct ?? this.qemuCpuPct,
      qemuRssBytes: qemuRssBytes ?? this.qemuRssBytes,
      guestReady: guestReady ?? this.guestReady,
      freshness: freshness ?? this.freshness,
    );
  }
}

