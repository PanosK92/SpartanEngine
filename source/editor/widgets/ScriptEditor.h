#pragma once


#include "Widget.h"
#include "imgui/TextEditor.h"

class ScriptEditor : public Widget
{
public:
    ScriptEditor(Editor* editor);

    void OnTickVisible() override;

private:

    std::string script_file;

    static constexpr size_t buffer_size = 1024 * 64;
    char m_buffer[buffer_size]      = {0};

    TextEditor TextEditor;
};
