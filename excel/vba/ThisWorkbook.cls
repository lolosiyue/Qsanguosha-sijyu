VERSION 1.0 CLASS
Attribute VB_Name = "ThisWorkbook"
Attribute VB_PredeclaredId = True
Option Explicit
Private Sub Workbook_Open()
    QsanTests.RegisterWorkbookHooks
End Sub
Private Sub Workbook_BeforeClose(Cancel As Boolean)
    QsanStop
End Sub
