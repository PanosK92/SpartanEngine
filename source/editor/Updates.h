/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

class Editor;

// every check against github lives here: engine releases, the project package and the worlds,
// together with the downloads that install them and the prompts that offer them
class Updates
{
public:
    enum class InstallResult
    {
        None,
        Installed,
        ProjectFailed,
        WorldsFailed
    };

    enum class CheckStatus
    {
        Idle,
        Checking,
        UpToDate,
        Failed
    };

    static void Initialize(Editor* editor);

    // content prompts only show over the world launcher, never over a world that is being played or edited
    static void Tick(bool allow_engine_prompt, bool launcher_visible);

    // a manual check ignores updates that were skipped before
    static void CheckNow();
    static CheckStatus GetCheckStatus();

    // first run, the launcher asks for whatever is missing on disk
    static void InstallContent(bool install_project, bool install_worlds);
    static InstallResult TakeInstallResult();
    // why the last install failed (the step, the file and the system or curl error), empty when it did not fail
    static const std::string& GetInstallError();

    static bool IsBusy();
    static bool IsPromptVisible();
    static bool IsProjectInstalled();
    static bool IsWorldsDirectoryAClone();
};
