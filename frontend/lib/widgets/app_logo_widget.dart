import 'dart:io';

import 'package:flutter/material.dart';

import '../core/theme.dart';

/// Renders authentic application logos for registered and running applications.
/// Supports native macOS app icon extraction (.icns via sips) as well as
/// high-fidelity branded vector badges for creative and developer suites.
class AppLogoWidget extends StatefulWidget {
  final String appName;
  final String executablePath;
  final double size;

  const AppLogoWidget({
    super.key,
    required this.appName,
    required this.executablePath,
    this.size = 38.0,
  });

  @override
  State<AppLogoWidget> createState() => _AppLogoWidgetState();
}

class _AppLogoWidgetState extends State<AppLogoWidget> {
  String? _extractedIconPath;
  bool _hasAttemptedExtract = false;

  @override
  void initState() {
    super.initState();
    _resolveAppIcon();
  }

  @override
  void didUpdateWidget(covariant AppLogoWidget oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.executablePath != widget.executablePath ||
        oldWidget.appName != widget.appName) {
      _hasAttemptedExtract = false;
      _resolveAppIcon();
    }
  }

  void _resolveAppIcon() async {
    final lowerName = widget.appName.toLowerCase();
    final lowerPath = widget.executablePath.toLowerCase();

    // 1. Direct bundled assets
    if (lowerName.contains('lightroom') || lowerPath.contains('lightroom')) {
      return; // Uses assets/images/app_logos/adobe_lightroom.png directly in build
    }

    // 2. macOS .app bundle icon extractor
    if (!_hasAttemptedExtract &&
        Platform.isMacOS &&
        widget.executablePath.contains('.app')) {
      _hasAttemptedExtract = true;
      try {
        final path = widget.executablePath;
        final appIndex = path.indexOf('.app');
        if (appIndex != -1) {
          final bundlePath = path.substring(0, appIndex + 4);
          final resDir = Directory('$bundlePath/Contents/Resources');
          if (await resDir.exists()) {
            final files = await resDir.list().toList();
            File? bestIcns;
            for (final f in files) {
              if (f is File && f.path.endsWith('.icns')) {
                final fname = f.path.split('/').last.toLowerCase();
                if (fname.contains('app') || fname.contains('icon')) {
                  bestIcns = f;
                  break;
                }
                bestIcns ??= f;
              }
            }

            if (bestIcns != null) {
              final cleanName = widget.appName.toLowerCase().replaceAll(
                RegExp(r'[^a-z0-9_]'),
                '_',
              );
              final outDir = Directory('/tmp/veylora_app_icons');
              if (!outDir.existsSync()) {
                outDir.createSync(recursive: true);
              }
              final outPng = File('${outDir.path}/$cleanName.png');
              if (!outPng.existsSync()) {
                await Process.run('/usr/bin/sips', [
                  '-s',
                  'format',
                  'png',
                  bestIcns.path,
                  '--out',
                  outPng.path,
                ]);
              }
              if (await outPng.exists() && mounted) {
                setState(() => _extractedIconPath = outPng.path);
              }
            }
          }
        }
      } catch (_) {
        // Fall back gracefully to high-fidelity brand badge
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final lowerName = widget.appName.toLowerCase();
    final lowerPath = widget.executablePath.toLowerCase();

    // 1. Adobe Lightroom Authentic Logo Asset
    if (lowerName.contains('lightroom') || lowerPath.contains('lightroom')) {
      return _buildImageContainer(
        Image.asset(
          'assets/images/app_logos/adobe_lightroom.png',
          width: widget.size,
          height: widget.size,
          fit: BoxFit.cover,
          errorBuilder: (_, _, _) => _buildAdobeBadge(
            'Lr',
            const Color(0xFF001E36),
            const Color(0xFF31A8FF),
          ),
        ),
      );
    }

    // 2. Extracted macOS Icon from disk
    if (_extractedIconPath != null && File(_extractedIconPath!).existsSync()) {
      return _buildImageContainer(
        Image.file(
          File(_extractedIconPath!),
          width: widget.size,
          height: widget.size,
          fit: BoxFit.cover,
          errorBuilder: (_, _, _) => _buildFallbackBadge(),
        ),
      );
    }

    // 3. High-fidelity Branded Badges for Popular Software
    if (lowerName.contains('photoshop') || lowerPath.contains('photoshop')) {
      return _buildAdobeBadge(
        'Ps',
        const Color(0xFF001E36),
        const Color(0xFF31A8FF),
      );
    } else if (lowerName.contains('illustrator') ||
        lowerPath.contains('illustrator')) {
      return _buildAdobeBadge(
        'Ai',
        const Color(0xFF330000),
        const Color(0xFFFF9A00),
      );
    } else if (lowerName.contains('premiere') ||
        lowerPath.contains('premiere')) {
      return _buildAdobeBadge(
        'Pr',
        const Color(0xFF240038),
        const Color(0xFFEA77FF),
      );
    } else if (lowerName.contains('after effects') ||
        lowerPath.contains('after effects')) {
      return _buildAdobeBadge(
        'Ae',
        const Color(0xFF1E0038),
        const Color(0xFFD291FF),
      );
    } else if (lowerName.contains('blender') || lowerPath.contains('blender')) {
      return _buildBrandBadge(
        'BL',
        const Color(0xFFE87D0D),
        Icons.view_in_ar_rounded,
      );
    } else if (lowerName.contains('code') ||
        lowerName.contains('vscode') ||
        lowerPath.contains('visual studio code')) {
      return _buildBrandBadge(
        'VS',
        const Color(0xFF007ACC),
        Icons.code_rounded,
      );
    } else if (lowerName.contains('python') || lowerPath.contains('python')) {
      return _buildBrandBadge(
        'PY',
        const Color(0xFF3776AB),
        Icons.terminal_rounded,
      );
    } else if (lowerName.contains('terminal') ||
        lowerName.contains('bash') ||
        lowerName.contains('sh')) {
      return _buildBrandBadge(
        'SH',
        const Color(0xFF1E293B),
        Icons.terminal_rounded,
      );
    }

    return _buildFallbackBadge();
  }

  Widget _buildImageContainer(Widget image) {
    return Container(
      width: widget.size,
      height: widget.size,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: 0.18),
            blurRadius: 6,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      clipBehavior: Clip.antiAlias,
      child: image,
    );
  }

  Widget _buildAdobeBadge(String letters, Color bg, Color borderAccent) {
    return Container(
      width: widget.size,
      height: widget.size,
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: borderAccent, width: 1.8),
        boxShadow: [
          BoxShadow(
            color: borderAccent.withValues(alpha: 0.25),
            blurRadius: 6,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      alignment: Alignment.center,
      child: Text(
        letters,
        style: TextStyle(
          fontSize: widget.size * 0.42,
          fontWeight: FontWeight.w800,
          color: borderAccent,
          letterSpacing: -0.5,
          fontFamily: VeyloraTypography.fontSans,
        ),
      ),
    );
  }

  Widget _buildBrandBadge(String code, Color accent, IconData icon) {
    return Container(
      width: widget.size,
      height: widget.size,
      decoration: BoxDecoration(
        color: accent.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: accent.withValues(alpha: 0.4), width: 1.2),
      ),
      alignment: Alignment.center,
      child: Icon(icon, size: widget.size * 0.48, color: accent),
    );
  }

  Widget _buildFallbackBadge() {
    final colors = VosColors.of(context);
    return Container(
      width: widget.size,
      height: widget.size,
      decoration: BoxDecoration(
        color: colors.primary.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: colors.border),
      ),
      alignment: Alignment.center,
      child: Icon(
        Icons.terminal_rounded,
        size: widget.size * 0.45,
        color: colors.primary,
      ),
    );
  }
}
