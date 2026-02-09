#pragma once


#include "Widget.h"
#include "ImGui/TextEditor.h"

class ScriptEditor : public Widget
{
public:
    ScriptEditor(Editor* editor);

    void OnTickVisible() override;

private:

    std::string script_file;

    static constexpr size_t kBufferSize = 1024 * 64;
    char m_buffer[kBufferSize]      = {0};

    TextEditor TextEditor;
};
