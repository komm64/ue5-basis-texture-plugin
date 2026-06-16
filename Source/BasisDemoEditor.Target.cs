using UnrealBuildTool;
using System.Collections.Generic;

public class BasisDemoEditorTarget : TargetRules
{
    public BasisDemoEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BasisDemo");
    }
}
