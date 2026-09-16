#ifndef QSAN_ANDROID_DIALOG_FIT_H
#define QSAN_ANDROID_DIALOG_FIT_H

class QApplication;

// Fits Android dialogs and opt-in desktop responsive dialogs. Existing
// dialog signals and QDialog::accept()/reject() semantics remain untouched.
void installAndroidDialogFit(QApplication *application);

#endif
