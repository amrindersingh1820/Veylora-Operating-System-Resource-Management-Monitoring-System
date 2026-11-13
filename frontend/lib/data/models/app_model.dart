class DiscoveredApp {
  final String name;
  final String path;
  final String package;
  final String category;

  DiscoveredApp({
    required this.name,
    required this.path,
    required this.package,
    this.category = 'System Tool',
  });

  factory DiscoveredApp.fromJson(Map<String, dynamic> json) {
    return DiscoveredApp(
      name: json['name'] as String? ?? '',
      path: json['path'] as String? ?? '',
      package: json['package'] as String? ?? '',
      category: json['category'] as String? ?? 'System Tool',
    );
  }
}

class RegisteredApp {
  final String appId;
  final String vmId;
  final String name;
  final String executablePath;
  final String description;
  final String category;
  final String version;
  final String defaultProfileId;
  final bool enabled;
  final int launchCount;

  RegisteredApp({
    required this.appId,
    required this.vmId,
    required this.name,
    required this.executablePath,
    required this.description,
    required this.category,
    required this.version,
    required this.defaultProfileId,
    required this.enabled,
    required this.launchCount,
  });

  factory RegisteredApp.fromJson(Map<String, dynamic> json) {
    return RegisteredApp(
      appId: json['app_id'] as String? ?? '',
      vmId: json['vm_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      executablePath: json['path'] as String? ?? '',
      description: json['description'] as String? ?? '',
      category: json['category'] as String? ?? 'General',
      version: json['version'] as String? ?? '1.0',
      defaultProfileId: json['default_profile_id'] as String? ?? 'default',
      enabled: (json['enabled'] as num?)?.toInt() == 1,
      launchCount: (json['launch_count'] as num?)?.toInt() ?? 0,
    );
  }
}

class AppInstance {
  final String instanceId;
  final String appId;
  final String vmId;
  final int pid;
  final int startTime;
  final String cgroup;
  final String state;
  final String profileId;
  final int startedAt;
  final int stoppedAt;
  final int exitCode;
  final String failureReason;

  AppInstance({
    required this.instanceId,
    required this.appId,
    required this.vmId,
    required this.pid,
    required this.startTime,
    required this.cgroup,
    required this.state,
    required this.profileId,
    required this.startedAt,
    required this.stoppedAt,
    required this.exitCode,
    required this.failureReason,
  });

  bool get isRunning => state == 'RUNNING';
  bool get isStopped => state == 'STOPPED';
  String get status => state;
  double get cpuUsagePct => 0.0;
  int get memoryUsageBytes => 0;

  factory AppInstance.fromJson(Map<String, dynamic> json) {
    return AppInstance(
      instanceId: json['instance_id'] as String? ?? '',
      appId: json['app_id'] as String? ?? '',
      vmId: json['vm_id'] as String? ?? '',
      pid: (json['pid'] as num?)?.toInt() ?? 0,
      startTime: (json['start_time'] as num?)?.toInt() ?? 0,
      cgroup: json['cgroup'] as String? ?? '',
      state: json['state'] as String? ?? 'UNKNOWN',
      profileId: json['profile_id'] as String? ?? 'default',
      startedAt: (json['started_at'] as num?)?.toInt() ?? 0,
      stoppedAt: (json['stopped_at'] as num?)?.toInt() ?? 0,
      exitCode: (json['exit_code'] as num?)?.toInt() ?? -1,
      failureReason: json['failure_reason'] as String? ?? '',
    );
  }
}

class AppMetricsSample {
  final String instanceId;
  final String appId;
  final String vmId;
  final double cpuUsagePct;
  final int cpuTimeUsec;
  final int throttledUsec;
  final int memoryCurrentBytes;
  final int memoryPeakBytes;
  final int pidsCurrent;
  final int pidsMax;
  final int ioReadBytes;
  final int ioWriteBytes;
  final String freshness;
  final String verification;

  AppMetricsSample({
    required this.instanceId,
    required this.appId,
    required this.vmId,
    required this.cpuUsagePct,
    required this.cpuTimeUsec,
    required this.throttledUsec,
    required this.memoryCurrentBytes,
    required this.memoryPeakBytes,
    required this.pidsCurrent,
    required this.pidsMax,
    required this.ioReadBytes,
    required this.ioWriteBytes,
    required this.freshness,
    required this.verification,
  });

  factory AppMetricsSample.fromJson(Map<String, dynamic> json) {
    return AppMetricsSample(
      instanceId: json['instance_id'] as String? ?? '',
      appId: json['app_id'] as String? ?? '',
      vmId: json['vm_id'] as String? ?? '',
      cpuUsagePct: (json['cpu_usage_pct'] as num?)?.toDouble() ?? 0.0,
      cpuTimeUsec: (json['cpu_time_usec'] as num?)?.toInt() ?? 0,
      throttledUsec: (json['throttled_usec'] as num?)?.toInt() ?? 0,
      memoryCurrentBytes: (json['memory_current_bytes'] as num?)?.toInt() ?? 0,
      memoryPeakBytes: (json['memory_peak_bytes'] as num?)?.toInt() ?? 0,
      pidsCurrent: (json['pids_current'] as num?)?.toInt() ?? 0,
      pidsMax: (json['pids_max'] as num?)?.toInt() ?? -1,
      ioReadBytes: (json['io_read_bytes'] as num?)?.toInt() ?? 0,
      ioWriteBytes: (json['io_write_bytes'] as num?)?.toInt() ?? 0,
      freshness: json['freshness'] as String? ?? 'UNAVAILABLE',
      verification: json['verification'] as String? ?? 'UNVERIFIED',
    );
  }
}

