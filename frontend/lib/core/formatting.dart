import 'package:intl/intl.dart';

class VosFormat {
  static String formatBytes(int bytes) {
    if (bytes <= 0) return '0 B';
    const suffixes = ['B', 'KB', 'MB', 'GB', 'TB'];
    var i = 0;
    double dBytes = bytes.toDouble();
    while (dBytes >= 1024 && i < suffixes.length - 1) {
      dBytes /= 1024;
      i++;
    }
    return '${dBytes.toStringAsFixed(i == 0 ? 0 : 2)} ${suffixes[i]}';
  }

  static String formatPercent(double pct) {
    return '${pct.toStringAsFixed(1)}%';
  }

  static String formatTimestamp(int epochSeconds) {
    if (epochSeconds <= 0) return 'Never';
    final dt = DateTime.fromMillisecondsSinceEpoch(epochSeconds * 1000);
    return DateFormat('yyyy-MM-dd HH:mm:ss').format(dt);
  }

  static String formatTimestampMs(int epochMs) {
    if (epochMs <= 0) return 'Never';
    final dt = DateTime.fromMillisecondsSinceEpoch(epochMs);
    return DateFormat('HH:mm:ss').format(dt);
  }

  static String formatUptime(int seconds) {
    if (seconds <= 0) return '0s';
    final duration = Duration(seconds: seconds);
    final hours = duration.inHours;
    final minutes = duration.inMinutes.remainder(60);
    final secs = duration.inSeconds.remainder(60);
    if (hours > 0) {
      return '${hours}h ${minutes}m ${secs}s';
    } else if (minutes > 0) {
      return '${minutes}m ${secs}s';
    } else {
      return '${secs}s';
    }
  }

  static String formatBandwidth(int bytesPerSec) {
    return '${formatBytes(bytesPerSec)}/s';
  }
}

