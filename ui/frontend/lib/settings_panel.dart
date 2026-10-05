import 'package:flutter/material.dart';

import 'settings.dart';

/// The settings drawer, opened from the gear at the right of the top bar.
/// Changes apply (and are saved) as they're made.
class SettingsPanel extends StatelessWidget {
  const SettingsPanel({super.key, required this.settings});

  final DisplaySettings settings;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Drawer(
      width: 360,
      child: SafeArea(
        child: ListenableBuilder(
          listenable: settings,
          builder: (context, _) => ListView(
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
            children: [
              Row(
                children: [
                  Text('Settings', style: theme.textTheme.titleLarge),
                  const Spacer(),
                  IconButton(
                    icon: const Icon(Icons.close),
                    tooltip: 'Close',
                    onPressed: () => Navigator.of(context).pop(),
                  ),
                ],
              ),
              _heading(context, 'Units'),
              _choice<UnitPreset>(
                context,
                'Preset',
                UnitPreset.values,
                settings.preset,
                (p) => p.label,
                settings.applyPreset,
              ),
              _choice(context, 'Speed', SpeedUnit.values, settings.speed, (u) => u.label, (u) => settings.speed = u),
              _choice(context, 'Altitude', AltitudeUnit.values, settings.altitude, (u) => u.label,
                  (u) => settings.altitude = u),
              _choice(context, 'Vertical rate', VerticalRateUnit.values, settings.verticalRate, (u) => u.label,
                  (u) => settings.verticalRate = u),
              _choice(context, 'Distance', DistanceUnit.values, settings.distance, (u) => u.label,
                  (u) => settings.distance = u),
              _choice(context, 'Pressure', PressureUnit.values, settings.pressure, (u) => u.label,
                  (u) => settings.pressure = u),
              _heading(context, 'Time'),
              _choice(context, 'Clock', ClockFormat.values, settings.clock, (c) => c.label, (c) => settings.clock = c),
              _choice(context, 'Time zone', TimeZoneMode.values, settings.timeZone, (z) => z.label,
                  (z) => settings.timeZone = z),
            ],
          ),
        ),
      ),
    );
  }

  Widget _heading(BuildContext context, String text) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(top: 16, bottom: 4),
      child: Text(text, style: theme.textTheme.titleSmall?.copyWith(color: theme.colorScheme.primary)),
    );
  }

  /// A labelled row of segments, one per value. `selected` may be null (a
  /// preset when the units are a mix), leaving none highlighted.
  Widget _choice<T>(BuildContext context, String label, List<T> values, T? selected, String Function(T) labelOf,
      ValueChanged<T> onChanged) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: Theme.of(context).textTheme.labelMedium),
          const SizedBox(height: 4),
          SizedBox(
            width: double.infinity,
            child: SegmentedButton<T>(
              segments: [for (final v in values) ButtonSegment(value: v, label: Text(labelOf(v)))],
              selected: {if (selected != null) selected},
              emptySelectionAllowed: true,
              showSelectedIcon: false,
              style: const ButtonStyle(visualDensity: VisualDensity.compact),
              onSelectionChanged: (s) {
                // Tapping the selected segment empties the set; leave it selected.
                if (s.isNotEmpty) onChanged(s.first);
              },
            ),
          ),
        ],
      ),
    );
  }
}
