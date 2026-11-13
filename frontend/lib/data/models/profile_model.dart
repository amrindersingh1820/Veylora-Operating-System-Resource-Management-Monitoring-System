class ResourceProfile {
  final String profileId;
  final String name;
  final String description;
  final double cpuCores;
  final int cpuWeight;
  final int memoryMaxBytes;
  final int pidsMax;
  final int ioReadBps;
  final int ioWriteBps;
  final String gpuSupport;

  ResourceProfile({
    required this.profileId,
    required this.name,
    required this.description,
    required this.cpuCores,
    required this.cpuWeight,
    required this.memoryMaxBytes,
    required this.pidsMax,
    required this.ioReadBps,
    required this.ioWriteBps,
    this.gpuSupport = 'UNSUPPORTED',
  });

  bool get isDefault => profileId == 'default';

  factory ResourceProfile.fromJson(Map<String, dynamic> json) {
    return ResourceProfile(
      profileId: json['profile_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      description: json['description'] as String? ?? '',
      cpuCores: (json['cpu_cores'] as num?)?.toDouble() ?? 0.0,
      cpuWeight: (json['cpu_weight'] as num?)?.toInt() ?? 100,
      memoryMaxBytes: (json['memory_max_bytes'] as num?)?.toInt() ?? 0,
      pidsMax: (json['pids_max'] as num?)?.toInt() ?? -1,
      ioReadBps: (json['io_read_bps'] as num?)?.toInt() ?? 0,
      ioWriteBps: (json['io_write_bps'] as num?)?.toInt() ?? 0,
      gpuSupport: json['gpu_support'] as String? ?? 'UNSUPPORTED',
    );
  }
}

