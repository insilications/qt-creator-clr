/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "clangrefactoringengine.h"
#include "clangeditordocumentprocessor.h"

#include "clangdclient.h"
#include "clangmodelmanagersupport.h"

#include <cpptools/cppmodelmanager.h>
#include <languageclient/languageclientsymbolsupport.h>
#include <projectexplorer/session.h>
#include <utils/textutils.h>
#include <utils/qtcassert.h>

namespace ClangCodeModel {
namespace Internal {

void RefactoringEngine::startLocalRenaming(const CppTools::CursorInEditor &data,
                                           CppTools::ProjectPart *,
                                           RenameCallback &&renameSymbolsCallback)
{
    ClangEditorDocumentProcessor *processor = ClangEditorDocumentProcessor::get(
        data.filePath().toString());
    const int startRevision = data.cursor().document()->revision();

    using ClangBackEnd::SourceLocationsContainer;
    auto defaultCallback = [renameSymbolsCallback, startRevision]() {
        return renameSymbolsCallback(QString(), SourceLocationsContainer{}, startRevision);
    };

    if (!processor)
        return defaultCallback();

    QFuture<CppTools::CursorInfo> cursorFuture = processor->requestLocalReferences(data.cursor());
    if (cursorFuture.isCanceled())
        return defaultCallback();

    if (m_watcher)
        m_watcher->cancel();

    m_watcher.reset(new FutureCursorWatcher());
    QObject::connect(m_watcher.get(), &FutureCursorWatcher::finished, [=]() {
        if (m_watcher->isCanceled())
            return defaultCallback();
        const CppTools::CursorInfo info = m_watcher->result();
        if (info.useRanges.empty())
            return defaultCallback();

        QTextCursor cursor = Utils::Text::wordStartCursor(data.cursor());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
                            info.useRanges.first().length);
        const QString symbolName = cursor.selectedText();
        ClangBackEnd::SourceLocationsContainer container;
        for (auto& use : info.useRanges) {
            container.insertSourceLocation(ClangBackEnd::FilePathId(),
                                           use.line,
                                           use.column,
                                           use.length);
        }
        renameSymbolsCallback(symbolName, container, data.cursor().document()->revision());
    });

    m_watcher->setFuture(cursorFuture);
}

void RefactoringEngine::findUsages(const CppTools::CursorInEditor &cursor,
                                   CppTools::UsagesCallback &&callback) const
{
    ProjectExplorer::Project * const project
            = ProjectExplorer::SessionManager::projectForFile(cursor.filePath());
    ClangdClient * const client = ClangModelManagerSupport::instance()->clientForProject(project);
    if (!client || !client->isFullyIndexed()) {
        CppTools::CppModelManager::builtinRefactoringEngine()
                ->findUsages(cursor, std::move(callback));
        return;
    }
    // TODO: We want to keep our "access type info" feature.
    //       Check whether we can support it using clang 12's textDocument/ast request
    if (!client->documentOpen(cursor.textDocument()))
        client->openDocument(cursor.textDocument()); // TODO: Just a workaround
    client->symbolSupport().findUsages(cursor.textDocument(), cursor.cursor());
}

} // namespace Internal
} // namespace ClangCodeModel
