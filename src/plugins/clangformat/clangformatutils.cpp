/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "clangformatutils.h"

#include <coreplugin/icore.h>
#include <cpptools/cppcodestylesettings.h>
#include <texteditor/tabsettings.h>
#include <projectexplorer/project.h>
#include <projectexplorer/session.h>

using namespace clang;
using namespace format;
using namespace llvm;
using namespace CppTools;
using namespace ProjectExplorer;
using namespace TextEditor;

namespace ClangFormat {

static void applyTabSettings(clang::format::FormatStyle &style, const TabSettings &settings)
{
    style.IndentWidth = static_cast<unsigned>(settings.m_indentSize);
    style.TabWidth = static_cast<unsigned>(settings.m_tabSize);

    if (settings.m_tabPolicy == TabSettings::TabsOnlyTabPolicy)
        style.UseTab = FormatStyle::UT_Always;
    else if (settings.m_tabPolicy == TabSettings::SpacesOnlyTabPolicy)
        style.UseTab = FormatStyle::UT_Never;
    else
        style.UseTab = FormatStyle::UT_ForContinuationAndIndentation;

    if (settings.m_continuationAlignBehavior == TabSettings::NoContinuationAlign) {
        style.ContinuationIndentWidth = 0;
        style.AlignAfterOpenBracket = FormatStyle::BAS_DontAlign;
    } else {
        style.ContinuationIndentWidth = style.IndentWidth;
        style.AlignAfterOpenBracket = FormatStyle::BAS_Align;
    }
}

static void applyCppCodeStyleSettings(clang::format::FormatStyle &style,
                                      const CppCodeStyleSettings &settings)
{
    style.IndentCaseLabels = settings.indentSwitchLabels;
    style.AlignOperands = settings.alignAssignments;
    style.NamespaceIndentation = FormatStyle::NI_None;
    if (settings.indentNamespaceBody)
        style.NamespaceIndentation = FormatStyle::NI_All;

    style.BraceWrapping.IndentBraces = false;
    if (settings.indentBlockBraces) {
        if (settings.indentClassBraces && settings.indentEnumBraces
                && settings.indentNamespaceBraces && settings.indentFunctionBraces) {
            style.BraceWrapping.IndentBraces = true;
        } else {
            style.BreakBeforeBraces = FormatStyle::BS_GNU;
        }
    }

    if (settings.bindStarToIdentifier || settings.bindStarToRightSpecifier)
        style.PointerAlignment = FormatStyle::PAS_Right;
    else
        style.PointerAlignment = FormatStyle::PAS_Left;

    style.AccessModifierOffset = settings.indentAccessSpecifiers
            ? 0
            : - static_cast<int>(style.IndentWidth);
}

static Utils::FileName projectStylePath()
{
    const Project *project = SessionManager::startupProject();
    if (project)
        return project->projectDirectory();

    return Utils::FileName();
}

static Utils::FileName globalStylePath()
{
    return Utils::FileName::fromString(Core::ICore::userResourcePath());
}

Utils::FileName currentStyleConfigPath()
{
    Utils::FileName path = projectStylePath();
    if (!path.isEmpty())
        return path;

    return globalStylePath();
}

static clang::format::FormatStyle constructStyle(bool isGlobal)
{
    FormatStyle style = getLLVMStyle();
    style.BreakBeforeBraces = FormatStyle::BS_Custom;

    const CppCodeStyleSettings codeStyleSettings = isGlobal
            ? CppCodeStyleSettings::currentGlobalCodeStyle()
            : CppCodeStyleSettings::currentProjectCodeStyle()
              .value_or(CppCodeStyleSettings::currentGlobalCodeStyle());
    const TabSettings tabSettings = isGlobal
            ? CppCodeStyleSettings::currentGlobalTabSettings()
            : CppCodeStyleSettings::currentProjectTabSettings();

    applyTabSettings(style, tabSettings);
    applyCppCodeStyleSettings(style, codeStyleSettings);

    return style;
}

void createStyleFileIfNeeded(Utils::FileName styleConfigPath, bool isGlobal)
{
    const QString configFile = styleConfigPath.appendPath(".clang-format").toString();
    if (QFile::exists(configFile))
        return;

    std::fstream newStyleFile(configFile.toStdString(), std::fstream::out);
    if (newStyleFile.is_open()) {
        newStyleFile << clang::format::configurationAsText(constructStyle(isGlobal));
        newStyleFile.close();
    }
}

static clang::format::FormatStyle currentStyle(bool isGlobal)
{
    Utils::FileName styleConfigPath = isGlobal ? globalStylePath() : projectStylePath();
    createStyleFileIfNeeded(styleConfigPath, isGlobal);

    Expected<FormatStyle> style = format::getStyle(
                "file", styleConfigPath.appendPath("test.cpp").toString().toStdString(), "LLVM");
    if (style)
        return *style;

    handleAllErrors(style.takeError(), [](const ErrorInfoBase &) {
        // do nothing
    });

    return constructStyle(isGlobal);
}

clang::format::FormatStyle currentProjectStyle()
{
    return currentStyle(false);
}

clang::format::FormatStyle currentGlobalStyle()
{
    return currentStyle(true);
}

clang::format::FormatStyle currentStyle()
{
    const bool isGlobal = !CppCodeStyleSettings::currentProjectCodeStyle().has_value();
    return currentStyle(isGlobal);
}

}
