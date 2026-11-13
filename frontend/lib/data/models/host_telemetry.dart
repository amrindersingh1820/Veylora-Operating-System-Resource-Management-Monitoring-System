class HostTelemetry {
  final int cpuCount;
  final double cpuUsagePct;
  final double cpuUserPct;
  final double cpuSystemPct;
  final double cpuIdlePct;
  final int totalRamBytes;
  final int usedRamBytes;
  final int freeRamBytes;
  final int availableRamBytes;
  final double loadAvg1m;
  final double loadAvg5m;
  final double loadAvg15m;
  final int timestampMs;
  final String cpuArch;
  final String hostModel;
  final String hostPlatform;

  HostTelemetry({
    required this.cpuCount,
    required this.cpuUsagePct,
    required this.cpuUserPct,
    required this.cpuSystemPct,
    required this.cpuIdlePct,
    required this.totalRamBytes,
    required this.usedRamBytes,
    required this.freeRamBytes,
    required this.availableRamBytes,
    required this.loadAvg1m,
    required this.loadAvg5m,
    required this.loadAvg15m,
    required this.timestampMs,
    required this.cpuArch,
    required this.hostModel,
    required this.hostPlatform,
  });

  factory HostTelemetry.fromJson(Map<String, dynamic> json) {
    return HostTelemetry(
      cpuCount: (json['cpu_count'] as num?)?.toInt() ?? 1,
      cpuUsagePct: (json['cpu_usage_pct'] as num?)?.toDouble() ?? 0.0,
      cpuUserPct: (json['cpu_user_pct'] as num?)?.toDouble() ?? 0.0,
      cpuSystemPct: (json['cpu_system_pct'] as num?)?.toDouble() ?? 0.0,
      cpuIdlePct: (json['cpu_idle_pct'] as num?)?.toDouble() ?? 0.0,
      totalRamBytes: (json['total_ram_bytes'] as num?)?.toInt() ?? 0,
      usedRamBytes: (json['used_ram_bytes'] as num?)?.toInt() ?? 0,
      freeRamBytes: (json['free_ram_bytes'] as num?)?.toInt() ?? 0,
      availableRamBytes: (json['available_ram_bytes'] as num?)?.toInt() ?? 0,
      loadAvg1m: (json['load_avg_1m'] as num?)?.toDouble() ?? 0.0,
      loadAvg5m: (json['load_avg_5m'] as num?)?.toDouble() ?? 0.0,
      loadAvg15m: (json['load_avg_15m'] as num?)?.toDouble() ?? 0.0,
      timestampMs: (json['timestamp_ms'] as num?)?.toInt() ?? 0,
      cpuArch: json['cpu_arch'] as String? ?? 'arm64',
      hostModel: json['host_model'] as String? ?? 'Apple Silicon',
      hostPlatform: json['host_platform'] as String? ?? 'macOS',
    );
  }

  static HostTelemetry empty() {
    return HostTelemetry(
      cpuCount: 1,
      cpuUsagePct: 0.0,
      cpuUserPct: 0.0,
      cpuSystemPct: 0.0,
      cpuIdlePct: 100.0,
      totalRamBytes: 0,
      usedRamBytes: 0,
      freeRamBytes: 0,
      availableRamBytes: 0,
      loadAvg1m: 0.0,
      loadAvg5m: 0.0,
      loadAvg15m: 0.0,
      timestampMs: 0,
      cpuArch: 'arm64',
      hostModel: 'Unknown',
      hostPlatform: 'macOS',
    );
  }
}

