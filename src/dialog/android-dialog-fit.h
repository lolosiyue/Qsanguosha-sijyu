#ifndef QSAN_ANDROID_DIALOG_FIT_H
#define QSAN_ANDROID_DIALOG_FIT_H

class QApplication;

// Installs the Android-only geometry adapter for widget dialogs.  Existing
// dialog signals and QDialog::accept()/reject() semantics remain untouched.
void installAndroidDialogFit(QApplication *application);

#endif
