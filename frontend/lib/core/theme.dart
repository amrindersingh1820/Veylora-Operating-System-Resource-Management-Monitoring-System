import 'package:flutter/material.dart';

/// ==============================================================================
/// VEYLORA DESIGN SYSTEM — CENTRAL TOKEN ARCHITECTURE
/// Inspired by Linear, Vercel, Raycast, Stripe Dashboard & Proxmox
/// ==============================================================================

/// Immutable corner radii tokens for Veylora
class VeyloraRadius {
  static const double xs = 4.0;
  static const double sm = 6.0;
  static const double md = 8.0;
  static const double lg = 12.0;
  static const double xl = 16.0;
  static const double full = 999.0;

  static final BorderRadius rXs = BorderRadius.circular(xs);
  static final BorderRadius rSm = BorderRadius.circular(sm);
  static final BorderRadius rMd = BorderRadius.circular(md);
  static final BorderRadius rLg = BorderRadius.circular(lg);
  static final BorderRadius rXl = BorderRadius.circular(xl);
  static final BorderRadius rFull = BorderRadius.circular(full);
}

/// Spacing scale tokens
class VeyloraSpacing {
  static const double xxs = 2.0;
  static const double xs = 4.0;
  static const double sm = 8.0;
  static const double md = 12.0;
  static const double lg = 16.0;
  static const double xl = 20.0;
  static const double xxl = 24.0;
  static const double xxxl = 32.0;
  static const double hero = 48.0;
}

/// Duration & Animation Curves
class VeyloraMotion {
  static const Duration fast = Duration(milliseconds: 140);
  static const Duration normal = Duration(milliseconds: 240);
  static const Duration smooth = Duration(milliseconds: 360);

  static const Curve ease = Curves.easeInOutCubic;
  static const Curve enter = Curves.easeOutCubic;
  static const Curve exit = Curves.easeInCubic;
}

/// Typography styling rules
class VeyloraTypography {
  static const String fontSans = 'Inter';
  static const String fontMono = 'monospace';

  // Numerical & Metric Displays
  static const TextStyle metricLarge = TextStyle(
    fontFamily: fontSans,
    fontSize: 26.0,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.8,
    height: 1.15,
    fontFeatures: [FontFeature.tabularFigures()],
  );

  static const TextStyle metricMedium = TextStyle(
    fontFamily: fontSans,
    fontSize: 20.0,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.5,
    fontFeatures: [FontFeature.tabularFigures()],
  );

  static const TextStyle metricSmall = TextStyle(
    fontFamily: fontSans,
    fontSize: 15.0,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.3,
    fontFeatures: [FontFeature.tabularFigures()],
  );

  // Monospace Values
  static const TextStyle mono = TextStyle(
    fontFamily: fontMono,
    fontSize: 11.5,
    letterSpacing: -0.2,
    height: 1.4,
  );

  static const TextStyle monoBold = TextStyle(
    fontFamily: fontMono,
    fontSize: 11.5,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.2,
  );

  // Headings
  static const TextStyle titleLarge = TextStyle(
    fontFamily: fontSans,
    fontSize: 22.0,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.6,
  );

  static const TextStyle titleMedium = TextStyle(
    fontFamily: fontSans,
    fontSize: 16.0,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.3,
  );

  static const TextStyle titleSmall = TextStyle(
    fontFamily: fontSans,
    fontSize: 13.5,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.2,
  );

  // Body & Labels
  static const TextStyle body = TextStyle(
    fontFamily: fontSans,
    fontSize: 13.0,
    fontWeight: FontWeight.w400,
    letterSpacing: -0.1,
    height: 1.45,
  );

  static const TextStyle bodyMedium = TextStyle(
    fontFamily: fontSans,
    fontSize: 13.0,
    fontWeight: FontWeight.w500,
    letterSpacing: -0.1,
  );

  static const TextStyle caption = TextStyle(
    fontFamily: fontSans,
    fontSize: 11.5,
    fontWeight: FontWeight.w400,
    letterSpacing: -0.1,
  );

  static const TextStyle captionMedium = TextStyle(
    fontFamily: fontSans,
    fontSize: 11.0,
    fontWeight: FontWeight.w500,
    letterSpacing: 0.1,
  );

  static const TextStyle overline = TextStyle(
    fontFamily: fontSans,
    fontSize: 10.0,
    fontWeight: FontWeight.w700,
    letterSpacing: 0.8,
  );
}

/// Semantic colors for Veylora (Linear & Vercel inspired).
class VosColors {
  // ==========================================================================
  // Premium Dark Palette (Linear, Raycast & macOS Tahoe/Sonoma Dark Mode)
  // Deep obsidian & refined graphite surfaces, crisp borders, high contrast
  // ==========================================================================
  static const Color darkBg = Color(0xFF0C0D12); // Deep velvet obsidian
  static const Color darkSurface = Color(0xFF13151D); // Differentiated sidebar & top surface
  static const Color darkCard = Color(0xFF181B26); // Clean elevated card surface
  static const Color darkCardHover = Color(0xFF222636); // Smooth card interactive hover
  static const Color darkBorder = Color(0xFF272D3F); // Crisp, subtle structural border
  static const Color darkBorderSubtle = Color(0xFF1D2230); // Very low-contrast inner dividers

  // ==========================================================================
  // Premium Light Palette (Soft neutral white, clean slate borders)
  // ==========================================================================
  static const Color lightBg = Color(0xFFF8FAFC);
  static const Color lightSurface = Color(0xFFFFFFFF);
  static const Color lightCard = Color(0xFFFFFFFF);
  static const Color lightCardHover = Color(0xFFF1F5F9);
  static const Color lightBorder = Color(0xFFE2E8F0);
  static const Color lightBorderSubtle = Color(0xFFEDF2F7);

  // ==========================================================================
  // Core Brand & Accent Colors (Electric blue with restrained violet)
  // ==========================================================================
  static const Color primary = Color(0xFF3B82F6); // Electric Blue
  static const Color primaryLight = Color(0xFF60A5FA);
  static const Color primaryDark = Color(0xFF2563EB);
  static const Color accent = Color(0xFF6366F1); // Restrained Violet / Indigo
  static const Color accentViolet = Color(0xFF8B5CF6);

  // Status & Semantic Feedback
  static const Color success = Color(0xFF10B981); // Emerald Green
  static const Color warning = Color(0xFFF59E0B); // Warm Amber
  static const Color danger = Color(0xFFEF4444); // Muted Crimson Red
  static const Color info = Color(0xFF38BDF8); // Sky Blue

  // Telemetry Layers (Four-Layer Virtualization Isolation Architecture)
  static const Color layerHost = Color(0xFF38BDF8); // Host Mach Native
  static const Color layerQemu = Color(0xFF818CF8); // QEMU Process VM
  static const Color layerGuest = Color(0xFF34D399); // Guest OS /proc/stat
  static const Color layerCgroup = Color(0xFFF472B6); // Per-App Cgroups v2

  // Text Hierarchy (Dark Theme)
  static const Color textMain = Color(0xFFF8FAFC); // Near-white (100% crisp legibility)
  static const Color textMuted = Color(0xFFA1AEC0); // Silky silver slate gray
  static const Color textDisabled = Color(0xFF6B768E); // Refined metadata slate

  // Text Hierarchy (Light Theme)
  static const Color lightTextMain = Color(0xFF0F172A); // Dark charcoal
  static const Color lightTextMuted = Color(0xFF64748B); // Muted slate
  static const Color lightTextDisabled = Color(0xFF94A3B8);

  /// Dynamic Theme Color Resolver based on current BuildContext
  static VosColorPalette of(BuildContext context) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    return isDark ? darkPalette : lightPalette;
  }

  static const VosColorPalette darkPalette = VosColorPalette(
    isDark: true,
    bg: darkBg,
    surface: darkSurface,
    card: darkCard,
    cardHover: darkCardHover,
    border: darkBorder,
    borderSubtle: darkBorderSubtle,
    textPrimary: textMain,
    textSecondary: textMuted,
    textMuted: textDisabled,
    primary: primary,
    accent: accent,
    success: success,
    warning: warning,
    danger: danger,
    info: info,
    layerHost: layerHost,
    layerQemu: layerQemu,
    layerGuest: layerGuest,
    layerCgroup: layerCgroup,
  );

  static const VosColorPalette lightPalette = VosColorPalette(
    isDark: false,
    bg: lightBg,
    surface: lightSurface,
    card: lightCard,
    cardHover: lightCardHover,
    border: lightBorder,
    borderSubtle: lightBorderSubtle,
    textPrimary: lightTextMain,
    textSecondary: lightTextMuted,
    textMuted: lightTextDisabled,
    primary: Color(0xFF2563EB),
    accent: Color(0xFF4F46E5),
    success: Color(0xFF059669),
    warning: Color(0xFFD97706),
    danger: Color(0xFFDC2626),
    info: Color(0xFF0284C7),
    layerHost: Color(0xFF0284C7),
    layerQemu: Color(0xFF6366F1),
    layerGuest: Color(0xFF059669),
    layerCgroup: Color(0xFFDB2777),
  );
}

class VosColorPalette {
  final bool isDark;
  final Color bg;
  final Color surface;
  final Color card;
  final Color cardHover;
  final Color border;
  final Color borderSubtle;
  final Color textPrimary;
  final Color textSecondary;
  final Color textMuted;
  final Color primary;
  final Color accent;
  final Color success;
  final Color warning;
  final Color danger;
  final Color info;
  final Color layerHost;
  final Color layerQemu;
  final Color layerGuest;
  final Color layerCgroup;

  const VosColorPalette({
    required this.isDark,
    required this.bg,
    required this.surface,
    required this.card,
    required this.cardHover,
    required this.border,
    required this.borderSubtle,
    required this.textPrimary,
    required this.textSecondary,
    required this.textMuted,
    required this.primary,
    required this.accent,
    required this.success,
    required this.warning,
    required this.danger,
    required this.info,
    required this.layerHost,
    required this.layerQemu,
    required this.layerGuest,
    required this.layerCgroup,
  });
}

class VosTheme {
  static ThemeData get darkTheme {
    return ThemeData(
      brightness: Brightness.dark,
      scaffoldBackgroundColor: VosColors.darkBg,
      primaryColor: VosColors.primary,
      cardColor: VosColors.darkCard,
      dividerColor: VosColors.darkBorder,
      colorScheme: const ColorScheme.dark(
        primary: VosColors.primary,
        secondary: VosColors.accent,
        surface: VosColors.darkCard,
        surfaceContainerHighest: VosColors.darkSurface,
        onSurface: VosColors.textMain,
        error: VosColors.danger,
      ),
      fontFamily: VeyloraTypography.fontSans,
      appBarTheme: const AppBarTheme(
        backgroundColor: VosColors.darkSurface,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: TextStyle(
          color: VosColors.textMain,
          fontSize: 16,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.3,
        ),
      ),
      cardTheme: CardThemeData(
        color: VosColors.darkCard,
        elevation: 0,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: VeyloraRadius.rLg,
          side: const BorderSide(color: VosColors.darkBorder, width: 1),
        ),
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: VosColors.darkCard,
        shape: RoundedRectangleBorder(
          borderRadius: VeyloraRadius.rLg,
          side: const BorderSide(color: VosColors.darkBorder, width: 1),
        ),
      ),
      popupMenuTheme: PopupMenuThemeData(
        color: VosColors.darkCard,
        shape: RoundedRectangleBorder(
          borderRadius: VeyloraRadius.rMd,
          side: const BorderSide(color: VosColors.darkBorder, width: 1),
        ),
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: VosColors.primary,
          foregroundColor: Colors.white,
          elevation: 0,
          textStyle: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rMd),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: VosColors.textMain,
          side: const BorderSide(color: VosColors.darkBorder),
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rMd),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: VosColors.primaryLight,
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rSm),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: VosColors.darkSurface,
        contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        border: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: VosColors.darkBorder),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: VosColors.darkBorder),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: VosColors.primary, width: 1.5),
        ),
        labelStyle: const TextStyle(color: VosColors.textMuted, fontSize: 13),
        hintStyle: const TextStyle(color: VosColors.textDisabled, fontSize: 13),
      ),
      dividerTheme: const DividerThemeData(
        color: VosColors.darkBorderSubtle,
        thickness: 1,
        space: 1,
      ),
    );
  }

  static ThemeData get lightTheme {
    return ThemeData(
      brightness: Brightness.light,
      scaffoldBackgroundColor: VosColors.lightBg,
      primaryColor: const Color(0xFF2563EB),
      cardColor: VosColors.lightCard,
      dividerColor: VosColors.lightBorder,
      colorScheme: const ColorScheme.light(
        primary: Color(0xFF2563EB),
        secondary: Color(0xFF4F46E5),
        surface: VosColors.lightSurface,
        error: VosColors.danger,
      ),
      fontFamily: VeyloraTypography.fontSans,
      appBarTheme: const AppBarTheme(
        backgroundColor: VosColors.lightSurface,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: TextStyle(
          color: VosColors.lightTextMain,
          fontSize: 16,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.3,
        ),
      ),
      cardTheme: CardThemeData(
        color: VosColors.lightCard,
        elevation: 0,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: VeyloraRadius.rLg,
          side: const BorderSide(color: VosColors.lightBorder, width: 1),
        ),
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF2563EB),
          foregroundColor: Colors.white,
          elevation: 0,
          textStyle: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rMd),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: VosColors.lightTextMain,
          side: const BorderSide(color: VosColors.lightBorder),
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rMd),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: const Color(0xFF2563EB),
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13, letterSpacing: -0.2),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rSm),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: const Color(0xFFF1F5F9),
        contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        border: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: VosColors.lightBorder),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: VosColors.lightBorder),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: VeyloraRadius.rMd,
          borderSide: const BorderSide(color: Color(0xFF2563EB), width: 1.5),
        ),
        labelStyle: const TextStyle(color: VosColors.lightTextMuted, fontSize: 13),
        hintStyle: const TextStyle(color: VosColors.lightTextDisabled, fontSize: 13),
      ),
      dividerTheme: const DividerThemeData(
        color: VosColors.lightBorder,
        thickness: 1,
        space: 1,
      ),
    );
  }
}
