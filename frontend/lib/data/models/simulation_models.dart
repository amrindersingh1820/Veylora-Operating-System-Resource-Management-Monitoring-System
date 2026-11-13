class SimProcess {
  final int pid;
  final String name;
  final int arrival;
  final int burst;
  final int priority;
  final int memKb;
  final List<int> maxResources;
  final List<int> heldResources;

  SimProcess({
    required this.pid,
    required this.name,
    this.arrival = 0,
    this.burst = 5,
    this.priority = 1,
    this.memKb = 128,
    this.maxResources = const [0, 0, 0, 0],
    this.heldResources = const [0, 0, 0, 0],
  });

  Map<String, dynamic> toJson() => {
    'pid': pid,
    'name': name,
    'arrival': arrival,
    'burst': burst,
    'priority': priority,
    'mem_kb': memKb,
    'max': maxResources,
    'held': heldResources,
  };
}

class GanttSegment {
  final int pid;
  final int start;
  final int end;

  GanttSegment({
    required this.pid,
    required this.start,
    required this.end,
  });

  bool get isIdle => pid == -1;

  factory GanttSegment.fromJson(Map<String, dynamic> json) {
    return GanttSegment(
      pid: (json['pid'] as num?)?.toInt() ?? -1,
      start: (json['start'] as num?)?.toInt() ?? 0,
      end: (json['end'] as num?)?.toInt() ?? 0,
    );
  }
}

class SimProcResult {
  final int pid;
  final String name;
  final int completion;
  final int tat;
  final int wt;
  final int rt;

  SimProcResult({
    required this.pid,
    required this.name,
    required this.completion,
    required this.tat,
    required this.wt,
    required this.rt,
  });

  factory SimProcResult.fromJson(Map<String, dynamic> json) {
    return SimProcResult(
      pid: (json['pid'] as num?)?.toInt() ?? 0,
      name: json['name'] as String? ?? '',
      completion: (json['completion'] as num?)?.toInt() ?? 0,
      tat: (json['tat'] as num?)?.toInt() ?? 0,
      wt: (json['wt'] as num?)?.toInt() ?? 0,
      rt: (json['rt'] as num?)?.toInt() ?? 0,
    );
  }
}

class SchedResultModel {
  final int algo;
  final String algoName;
  final int quantum;
  final String reason;
  final int totalTime;
  final int busyTime;
  final int ctxSwitches;
  final double cpuUtil;
  final double throughput;
  final double avgWt;
  final double avgTat;
  final double avgRt;
  final List<GanttSegment> gantt;
  final List<SimProcResult> processes;

  SchedResultModel({
    required this.algo,
    required this.algoName,
    required this.quantum,
    required this.reason,
    required this.totalTime,
    required this.busyTime,
    required this.ctxSwitches,
    required this.cpuUtil,
    required this.throughput,
    required this.avgWt,
    required this.avgTat,
    required this.avgRt,
    required this.gantt,
    required this.processes,
  });

  factory SchedResultModel.fromJson(Map<String, dynamic> json) {
    return SchedResultModel(
      algo: (json['algo'] as num?)?.toInt() ?? 0,
      algoName: json['algo_name'] as String? ?? '',
      quantum: (json['quantum'] as num?)?.toInt() ?? 0,
      reason: json['reason'] as String? ?? '',
      totalTime: (json['total_time'] as num?)?.toInt() ?? 0,
      busyTime: (json['busy_time'] as num?)?.toInt() ?? 0,
      ctxSwitches: (json['ctx_switches'] as num?)?.toInt() ?? 0,
      cpuUtil: (json['cpu_util'] as num?)?.toDouble() ?? 0.0,
      throughput: (json['throughput'] as num?)?.toDouble() ?? 0.0,
      avgWt: (json['avg_wt'] as num?)?.toDouble() ?? 0.0,
      avgTat: (json['avg_tat'] as num?)?.toDouble() ?? 0.0,
      avgRt: (json['avg_rt'] as num?)?.toDouble() ?? 0.0,
      gantt: (json['gantt'] as List<dynamic>?)
              ?.map((e) => GanttSegment.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
      processes: (json['processes'] as List<dynamic>?)
              ?.map((e) => SimProcResult.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }
}

class MemAllocation {
  final int pid;
  final String name;
  final int size;
  final int block;
  final int blockSize;
  final int frag;
  final bool allocated;

  MemAllocation({
    required this.pid,
    required this.name,
    required this.size,
    required this.block,
    required this.blockSize,
    required this.frag,
    required this.allocated,
  });

  factory MemAllocation.fromJson(Map<String, dynamic> json) {
    return MemAllocation(
      pid: (json['pid'] as num?)?.toInt() ?? 0,
      name: json['name'] as String? ?? '',
      size: (json['size'] as num?)?.toInt() ?? 0,
      block: (json['block'] as num?)?.toInt() ?? 0,
      blockSize: (json['block_size'] as num?)?.toInt() ?? 0,
      frag: (json['frag'] as num?)?.toInt() ?? 0,
      allocated: (json['allocated'] as num?)?.toInt() == 1,
    );
  }
}

class MemResultModel {
  final int strategy;
  final String strategyName;
  final int totalMem;
  final int usedMem;
  final int freeMem;
  final int internalFrag;
  final int failed;
  final List<MemAllocation> allocations;

  MemResultModel({
    required this.strategy,
    required this.strategyName,
    required this.totalMem,
    required this.usedMem,
    required this.freeMem,
    required this.internalFrag,
    required this.failed,
    required this.allocations,
  });

  factory MemResultModel.fromJson(Map<String, dynamic> json) {
    return MemResultModel(
      strategy: (json['strategy'] as num?)?.toInt() ?? 0,
      strategyName: json['strategy_name'] as String? ?? '',
      totalMem: (json['total_mem'] as num?)?.toInt() ?? 0,
      usedMem: (json['used_mem'] as num?)?.toInt() ?? 0,
      freeMem: (json['free_mem'] as num?)?.toInt() ?? 0,
      internalFrag: (json['internal_frag'] as num?)?.toInt() ?? 0,
      failed: (json['failed'] as num?)?.toInt() ?? 0,
      allocations: (json['allocations'] as List<dynamic>?)
              ?.map((e) => MemAllocation.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }
}

class BankerResultModel {
  final bool valid;
  final bool safe;
  final String message;
  final List<int> sequence;

  BankerResultModel({
    required this.valid,
    required this.safe,
    required this.message,
    required this.sequence,
  });

  factory BankerResultModel.fromJson(Map<String, dynamic> json) {
    return BankerResultModel(
      valid: (json['valid'] as num?)?.toInt() == 1,
      safe: (json['safe'] as num?)?.toInt() == 1,
      message: json['message'] as String? ?? '',
      sequence: (json['sequence'] as List<dynamic>?)
              ?.map((e) => (e as num).toInt())
              .toList() ??
          [],
    );
  }
}

