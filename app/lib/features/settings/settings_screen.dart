import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/providers/providers.dart';
import '../../shared/utils/formatters.dart';

/// Экран настроек
class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final coreVersion = ref.watch(coreVersionProvider);
    final stats = ref.watch(indexStatsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
      ),
      body: ListView(
        children: [
          // About section
          _SectionHeader(title: 'About'),
          _SettingsTile(
            icon: Icons.info_outline,
            title: 'FamilyVault',
            subtitle: 'Version 0.1.0',
            trailing: Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
              decoration: BoxDecoration(
                color: colorScheme.primaryContainer,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Text(
                'Core v$coreVersion',
                style: theme.textTheme.labelSmall?.copyWith(
                  color: colorScheme.primary,
                ),
              ),
            ),
          ),

          const Divider(),

          // Display section
          _SectionHeader(title: 'Display'),
          _SettingsTile(
            icon: Icons.palette_outlined,
            title: 'Theme',
            subtitle: 'System',
            onTap: () => _showThemeDialog(context),
          ),
          _SettingsTile(
            icon: Icons.grid_view,
            title: 'Default View',
            subtitle: 'Grid',
            onTap: () => _showViewModeDialog(context),
          ),

          const Divider(),

          // Storage section
          _SectionHeader(title: 'Storage'),
          stats.when(
            data: (s) => _SettingsTile(
              icon: Icons.storage,
              title: 'Index Size',
              subtitle: '${formatFileCount(s.totalFiles)} • ${formatFileSize(s.totalSize)}',
            ),
            loading: () => _SettingsTile(
              icon: Icons.storage,
              title: 'Index Size',
              subtitle: 'Calculating...',
            ),
            error: (_, __) => _SettingsTile(
              icon: Icons.storage,
              title: 'Index Size',
              subtitle: 'Error',
            ),
          ),
          _SettingsTile(
            icon: Icons.delete_outline,
            title: 'Clear Cache',
            subtitle: 'Free up space by clearing thumbnails',
            onTap: () => _confirmClearCache(context, ref),
          ),

          const Divider(),

          // About section
          _SectionHeader(title: 'Help'),
          _SettingsTile(
            icon: Icons.bug_report_outlined,
            title: 'Report Issue',
            onTap: () {},
          ),
          _SettingsTile(
            icon: Icons.code,
            title: 'View Source',
            subtitle: 'github.com/familyvault',
            onTap: () {},
          ),

          const SizedBox(height: 32),
          
          // Footer
          Center(
            child: Text(
              'Made with ❤️ for families',
              style: theme.textTheme.bodySmall?.copyWith(
                color: colorScheme.outline,
              ),
            ),
          ),
          const SizedBox(height: 16),
        ],
      ),
    );
  }

  void _showThemeDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text('Theme'),
        children: [
          RadioListTile<int>(
            value: 0,
            groupValue: 0,
            title: const Text('System'),
            onChanged: (v) => Navigator.pop(context),
          ),
          RadioListTile<int>(
            value: 1,
            groupValue: 0,
            title: const Text('Light'),
            onChanged: (v) => Navigator.pop(context),
          ),
          RadioListTile<int>(
            value: 2,
            groupValue: 0,
            title: const Text('Dark'),
            onChanged: (v) => Navigator.pop(context),
          ),
        ],
      ),
    );
  }

  void _showViewModeDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text('Default View'),
        children: [
          RadioListTile<int>(
            value: 0,
            groupValue: 0,
            title: const Text('Grid'),
            onChanged: (v) => Navigator.pop(context),
          ),
          RadioListTile<int>(
            value: 1,
            groupValue: 0,
            title: const Text('List'),
            onChanged: (v) => Navigator.pop(context),
          ),
        ],
      ),
    );
  }

  Future<void> _confirmClearCache(BuildContext context, WidgetRef ref) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Clear Cache'),
        content: const Text(
          'This will delete all cached thumbnails. '
          'They will be regenerated as needed.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Clear'),
          ),
        ],
      ),
    );

    if (confirmed == true && context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Cache cleared')),
      );
    }
  }
}

class _SectionHeader extends StatelessWidget {
  final String title;

  const _SectionHeader({required this.title});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
      child: Text(
        title,
        style: Theme.of(context).textTheme.titleSmall?.copyWith(
              color: Theme.of(context).colorScheme.primary,
              fontWeight: FontWeight.bold,
            ),
      ),
    );
  }
}

class _SettingsTile extends StatelessWidget {
  final IconData icon;
  final String title;
  final String? subtitle;
  final Widget? trailing;
  final VoidCallback? onTap;

  const _SettingsTile({
    required this.icon,
    required this.title,
    this.subtitle,
    this.trailing,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return ListTile(
      leading: Icon(icon),
      title: Text(title),
      subtitle: subtitle != null ? Text(subtitle!) : null,
      trailing: trailing ?? (onTap != null ? const Icon(Icons.chevron_right) : null),
      onTap: onTap,
    );
  }
}

