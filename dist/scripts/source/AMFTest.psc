Scriptname AMFTest Hidden

; Functional test harness for AMF's Papyrus native-function binding (decisions doc S3) and
; per-save persistence channel (S10). Run from the console via "cgf" or a quest fragment;
; results are confirmed in ApocryphaMenuFramework.log, not visually - exactly what the author asked
; to be testable without him watching.

int Function AMF_Ping() native global
Function AMF_SetTestValue(string value) native global
string Function AMF_GetTestValue() native global

Function RunTest() global
    int pingResult = AMF_Ping()
    Debug.Trace("AMFTest: AMF_Ping() returned " + pingResult)

    string before = AMF_GetTestValue()
    Debug.Trace("AMFTest: value before set = " + before)

    AMF_SetTestValue("hello from Papyrus")
    string after = AMF_GetTestValue()
    Debug.Trace("AMFTest: value after set = " + after)
EndFunction
