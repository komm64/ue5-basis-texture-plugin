using UnrealBuildTool;
using System.Collections.Generic;

public class BasisDemoTarget : TargetRules
{
    public BasisDemoTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bOverrideBuildEnvironment = true;
        ExtraModuleNames.Add("BasisDemo");
    }
}
