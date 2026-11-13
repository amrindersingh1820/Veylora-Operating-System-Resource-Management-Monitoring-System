class VosConstants {
  static const String appTitle = 'Veylora';
  static const String appSubtitle =
      'Creative OS Hypervisor & Architecture Engine';
  static const String appTagline = 'Beyond Virtual. Beyond Limits.';
  static const String appCategory = 'Virtual Computing Environment';
  static const String appVersion = 'v1.6.0 Pro';
  static const String defaultDbPath = 'virtual_os.db';
  static const String libraryName = 'libvos_backend.dylib';

  static const Duration telemetryInterval = Duration(milliseconds: 1000);
  static const Duration reconciliationInterval = Duration(seconds: 5);

  static const List<String> vmArchitectures = ['aarch64', 'x86_64'];
  static const List<String> appCategories = [
    'System Utility',
    'Background Worker',
    'Web Service',
    'Database',
    'Benchmarking',
    'General',
  ];

  static const int schedAlgoAuto = 0;
  static const int schedAlgoFcfs = 1;
  static const int schedAlgoSjf = 2;
  static const int schedAlgoSrtf = 3;
  static const int schedAlgoPrioNp = 4;
  static const int schedAlgoPrioP = 5;
  static const int schedAlgoRr = 6;

  static const int memStrategyAuto = 0;
  static const int memStrategyFirst = 1;
  static const int memStrategyBest = 2;
  static const int memStrategyWorst = 3;
}
