Attribute VB_Name = "QsanTests"
Option Explicit
Public Sub RegisterWorkbookHooks()
    EnsureUIBootstrap
End Sub
Public Sub EnsureUIBootstrap()
    Static ready As Boolean
    If ready Then Exit Sub
    QsanUi_Bootstrap
    ready = True
End Sub
Public Function QsanJsonContractCheck() As Boolean
    Dim x As Variant, d As Object
    Set x = QsanJsonParse("{""id"":""900719925474099312345"",""ok"":true,""none"":null}")
    Set d = x: QsanJsonContractCheck = (CStr(d("id")) = "900719925474099312345" And d("ok") = True And IsNull(d("none")))
End Function
