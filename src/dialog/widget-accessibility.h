#ifndef WIDGET_ACCESSIBILITY_H
#define WIDGET_ACCESSIBILITY_H

class QObject;

// Install the desktop-widget accessibility fallback once for an application.
// Existing meaningful accessible names are preserved.
void installWidgetAccessibility(QObject *application);

#endif // WIDGET_ACCESSIBILITY_H
