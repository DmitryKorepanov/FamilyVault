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

          // Optimization section
          _SectionHeader(title: 'Index'),
          _MaxTextSizeTile(),
          _SettingsTile(
            icon: Icons.refresh,
            title: 'Rebuild Index',
            subtitle: 'Re-index all folders with current settings',
            onTap: () => _rebuildIndex(context, ref),
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

  Future<void> _rebuildIndex(BuildContext context, WidgetRef ref) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Rebuild Index'),
        content: const Text(
          'This will delete the current database and re-index all folders '
          'with the current Max Text Size setting.\n\n'
          'This may take several minutes for large libraries.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(context).colorScheme.error,
            ),
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Rebuild'),
          ),
        ],
      ),
    );

    if (confirmed == true && context.mounted) {
      try {
        // Get current folders with ALL metadata before deleting
        final indexService = ref.read(indexServiceProvider);
        final folders = await indexService.getFolders();
        
        if (folders.isEmpty) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('No folders to rebuild')),
          );
          return;
        }
        
        // Store folder metadata (path, name, visibility, enabled)
        final folderData = folders.map((f) => (
          path: f.path,
          name: f.name,
          visibility: f.defaultVisibility,
          enabled: f.enabled,
        )).toList();
        
        // Show progress dialog
        if (context.mounted) {
          showDialog(
            context: context,
            barrierDismissible: false,
            builder: (context) => const AlertDialog(
              content: Row(
                children: [
                  CircularProgressIndicator(),
                  SizedBox(width: 24),
                  Text('Rebuilding index...'),
                ],
              ),
            ),
          );
        }
        
        // Rebuild database
        final dbService = ref.read(databaseServiceProvider);
        await dbService.rebuildDatabase();
        
        // Re-add folders with original metadata and scan
        final foldersNotifier = ref.read(foldersNotifierProvider.notifier);
        
        for (final folder in folderData) {
          // Only scan if folder was enabled
          await foldersNotifier.addFolder(
            folder.path,
            name: folder.name,
            visibility: folder.visibility,
            autoScan: folder.enabled, // Don't scan disabled folders
          );
        }
        
        // Restore enabled state for disabled folders
        final newFolders = await indexService.getFolders();
        for (final folder in folderData) {
          if (!folder.enabled) {
            // Find the newly added folder by path and disable it
            final newFolder = newFolders.where((f) => f.path == folder.path).firstOrNull;
            if (newFolder != null) {
              await indexService.setFolderEnabled(newFolder.id, false);
            }
          }
        }
        
        // Invalidate providers to refresh UI
        ref.invalidate(indexStatsProvider);
        ref.invalidate(foldersNotifierProvider);
        
        if (context.mounted) {
          Navigator.pop(context); // Close progress dialog
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Rebuilt index for ${folderData.length} folders')),
          );
        }
      } catch (e) {
        if (context.mounted) {
          Navigator.pop(context); // Close progress dialog if open
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Rebuild failed: $e')),
          );
        }
      }
    }
  }
}

/// Настройка размера индексируемого текста
class _MaxTextSizeTile extends ConsumerStatefulWidget {
  const _MaxTextSizeTile();

  @override
  ConsumerState<_MaxTextSizeTile> createState() => _MaxTextSizeTileState();
}

class _MaxTextSizeTileState extends ConsumerState<_MaxTextSizeTile> {
  int _currentValue = 100;
  bool _loaded = false;

  @override
  void initState() {
    super.initState();
    _loadValue();
  }

  void _loadValue() {
    final indexService = ref.read(indexServiceProvider);
    setState(() {
      _currentValue = indexService.getMaxTextSizeKB();
      _loaded = true;
    });
  }

  void _showSizeDialog() {
    showDialog(
      context: context,
      builder: (context) => _MaxTextSizeDialog(
        currentValue: _currentValue,
        onChanged: (value) {
          final indexService = ref.read(indexServiceProvider);
          indexService.setMaxTextSizeKB(value);
          // Also update ContentIndexer
          final contentIndexer = ref.read(contentIndexerServiceProvider);
          contentIndexer.setMaxTextSizeKB(value);
          setState(() {
            _currentValue = value;
          });
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return _SettingsTile(
      icon: Icons.text_fields,
      title: 'Max Text Size',
      subtitle: _loaded ? '$_currentValue KB per file' : 'Loading...',
      onTap: _loaded ? _showSizeDialog : null,
    );
  }
}

class _MaxTextSizeDialog extends StatefulWidget {
  final int currentValue;
  final ValueChanged<int> onChanged;

  const _MaxTextSizeDialog({
    required this.currentValue,
    required this.onChanged,
  });

  @override
  State<_MaxTextSizeDialog> createState() => _MaxTextSizeDialogState();
}

class _MaxTextSizeDialogState extends State<_MaxTextSizeDialog> {
  late int _selectedValue;

  static const _options = [10, 50, 100, 200, 500];

  @override
  void initState() {
    super.initState();
    _selectedValue = widget.currentValue;
    // Ensure selected value is in options
    if (!_options.contains(_selectedValue)) {
      _selectedValue = 100;
    }
  }

  @override
  Widget build(BuildContext context) {
    return SimpleDialog(
      title: const Text('Max Text Size'),
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24),
          child: Text(
            'Maximum text to extract from each document for full-text search.',
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ),
        const SizedBox(height: 8),
        ..._options.map((value) => RadioListTile<int>(
          value: value,
          groupValue: _selectedValue,
          title: Text('$value KB'),
          subtitle: Text(_getDescription(value)),
          onChanged: (v) {
            if (v != null) {
              setState(() => _selectedValue = v);
              widget.onChanged(v);
              Navigator.pop(context);
            }
          },
        )),
      ],
    );
  }

  String _getDescription(int value) {
    return switch (value) {
      10 => 'Minimal (fast)',
      50 => 'Basic search',
      100 => 'Recommended',
      200 => 'Extended',
      500 => 'Full documents',
      _ => '',
    };
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

