class EventLogEntry {
  final int id;
  final String instanceId;
  final String appId;
  final String vmId;
  final String eventType;
  final String eventDetails;
  final int createdAt;

  EventLogEntry({
    required this.id,
    required this.instanceId,
    required this.appId,
    required this.vmId,
    required this.eventType,
    required this.eventDetails,
    required this.createdAt,
  });

  factory EventLogEntry.fromJson(Map<String, dynamic> json) {
    return EventLogEntry(
      id: (json['id'] as num?)?.toInt() ?? 0,
      instanceId: json['instance_id'] as String? ?? '',
      appId: json['app_id'] as String? ?? '',
      vmId: json['vm_id'] as String? ?? '',
      eventType: json['event_type'] as String? ?? '',
      eventDetails: json['event_details'] as String? ?? '',
      createdAt: (json['created_at'] as num?)?.toInt() ?? 0,
    );
  }
}

