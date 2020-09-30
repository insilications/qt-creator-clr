    return Utils::toSet(leftSelection).unite(Utils::toSet(rightSelection)).count();
    const int lineCount = lines.count();
    if (line < newLines.count()) {
    for (int line = 0; line < newLines.count(); ++line) {
        const int startPos = line > 0
                ? -1
                : lines->isEmpty() ? 0 : lines->last().text.count();
        const int endPos = line < newLines.count() - 1
                ? -1
                : lines->isEmpty() ? 0 : lines->last().text.count();
    while (i <= leftDiffList.count() && j <= rightDiffList.count()) {
        const Diff leftDiff = i < leftDiffList.count()
                ? leftDiffList.at(i)
                : Diff(Diff::Equal);
        const Diff rightDiff = j < rightDiffList.count()
                ? rightDiffList.at(j)
                : Diff(Diff::Equal);
            if (j == rightDiffList.count() && lastLineEqual && leftDiff.text.startsWith('\n'))
            if (j == rightDiffList.count())
            if (i == leftDiffList.count() && lastLineEqual && rightDiff.text.startsWith('\n'))
            if (i == leftDiffList.count())
            if (i < leftDiffList.count() || j < rightDiffList.count() || (!leftLines.isEmpty() && !rightLines.isEmpty())) {
                while (line < qMax(newLeftLines.count(), newRightLines.count())) {
                    const int commonLineCount = qMin(newLeftLines.count(),
                                                     newRightLines.count());
                                if (i == leftDiffList.count()
                                        && j == rightDiffList.count())
                    if ((line < commonLineCount - 1) // before the last common line in equality
                            || (line == commonLineCount - 1 // or the last common line in equality
                                && i == leftDiffList.count() // and it's the last iteration
                                && j == rightDiffList.count())) {
    for (int i = leftData.count(); i < rightData.count(); i++)
    for (int i = rightData.count(); i < leftData.count(); i++)
    const int visualLineCount = leftData.count();
    while (i < originalData.rows.count()) {
            while (i < originalData.rows.count()) {
            const bool last = i == originalData.rows.count(); // includes last line?
            const int lastLine = last
                    ? originalData.rows.count() : i - contextLineCount;
    while (i < originalData.rows.count()) {
        while (i < originalData.rows.count()) {
        const int rowCount = chunkData.rows.count();
    for (int i = 0; i <= chunkData.rows.count(); i++) {
        const RowData &rowData = i < chunkData.rows.count()
                ? chunkData.rows.at(i)
                : RowData(TextLineData(TextLineData::Separator)); // dummy,
                                        // ensure we process buffers to the end.
                                        // rowData will be equal
                for (int j = 0; j < leftBuffer.count(); j++) {
                                              leftBuffer.at(j).text,
                                              lastChunk,
                                              i == chunkData.rows.count()
                                                       && j == leftBuffer.count() - 1);
                for (int j = 0; j < rightBuffer.count(); j++) {
                                              rightBuffer.at(j).text,
                                              lastChunk,
                                              i == chunkData.rows.count()
                                                       && j == rightBuffer.count() - 1);
            if (i < chunkData.rows.count()) {
                                          rowData.rightLine.text,
                                          lastChunk,
                                          i == chunkData.rows.count() - 1);
    for (int i = 0; i < fileDataList.count(); i++) {
                for (int j = 0; j < fileData.chunks.count(); j++) {
                                     (j == fileData.chunks.count() - 1)
                                     && fileData.lastChunkAtTheEndOfFile);
static QList<RowData> readLines(QStringRef patch,
                                bool lastChunk,
                                bool *lastChunkAtTheEndOfFile,
                                bool *ok)
    const QVector<QStringRef> lines = patch.split(newLine);
    for (i = 0; i < lines.count(); i++) {
        QStringRef line = lines.at(i);
                i = lines.count(); // pretend as we've read all the lines (we just ignore the rest)
                    noNewLineInEqual = diffList.count() - 1;
                    noNewLineInDelete = diffList.count() - 1;
                    noNewLineInInsert = diffList.count() - 1;
                    i = lines.count(); // pretend as we've read all the lines (we just ignore the rest)
                lastEqual = diffList.count() - 1;
                lastDelete = diffList.count() - 1;
                lastInsert = diffList.count() - 1;
    if (i < lines.count() // we broke before
            // or we have noNewLine in some equal line and in either delete or insert line
            || (noNewLineInEqual >= 0 && (noNewLineInDelete >= 0 || noNewLineInInsert >= 0))
            // or we have noNewLine in not the last equal line
            || (noNewLineInEqual >= 0 && noNewLineInEqual != lastEqual)
            // or we have noNewLine in not the last delete line or there is a equal line after the noNewLine for delete
            || (noNewLineInDelete >= 0 && (noNewLineInDelete != lastDelete || lastEqual > lastDelete))
            // or we have noNewLine in not the last insert line or there is a equal line after the noNewLine for insert
            || (noNewLineInInsert >= 0 && (noNewLineInInsert != lastInsert || lastEqual > lastInsert))) {
        diff.text = diff.text.left(diff.text.count() - 1);
        diff.text = diff.text.left(diff.text.count() - 1);
        diff.text = diff.text.left(diff.text.count() - 1);
static QStringRef readLine(QStringRef text, QStringRef *remainingText, bool *hasNewLine)
            *remainingText = QStringRef();
static bool detectChunkData(QStringRef chunkDiff,
                            ChunkData *chunkData,
                            QStringRef *remainingPatch)
    const QStringRef chunkLine = readLine(chunkDiff, remainingPatch, &hasNewLine);
    QStringRef leftPos = chunkLine.mid(leftPosStart, leftPosLength);
    QStringRef rightPos = chunkLine.mid(rightPosStart, rightPosLength);
    const QStringRef optionalHint = chunkLine.mid(optionalHintStart, optionalHintLength);
static QList<ChunkData> readChunks(QStringRef patch,
                                   bool *lastChunkAtTheEndOfFile,
                                   bool *ok)
    const int count = startingPositions.count();
                // drop the newline before the next chunk start
                ? startingPositions.at(i + 1) - 1
                // drop the possible newline by the end of patch
                : (patch.at(patch.count() - 1) == newLine ? patch.count() - 1 : patch.count());
        const QStringRef chunkDiff = patch.mid(chunkStart, chunkEnd - chunkStart);
        QStringRef lines;
static FileData readDiffHeaderAndChunks(QStringRef headerAndChunks,
                                        bool *ok)
    QStringRef patch = headerAndChunks;
static QList<FileData> readDiffPatch(QStringRef patch,
                                     bool *ok,
                                     QFutureInterfaceBase *jobController)
                QStringRef headerAndChunks = patch.mid(lastPos,
                                                       pos - lastPos);
            QStringRef headerAndChunks = patch.mid(lastPos,
                                                   patch.count() - lastPos - 1);
static bool detectIndexAndBinary(QStringRef patch,
                                 FileData *fileData,
                                 QStringRef *remainingPatch)
    QStringRef afterNextLine;
    const QStringRef nextLine = readLine(patch, &afterNextLine, &hasNewLine);
        const QStringRef indices = nextLine.mid(indexHeader.size());
        *remainingPatch = QStringRef();
    QStringRef afterMinuses;
    const QStringRef minuses = readLine(*remainingPatch, &afterMinuses, &hasNewLine);
    QStringRef afterPluses;
    const QStringRef pluses = readLine(afterMinuses, &afterPluses, &hasNewLine);
static bool extractCommonFileName(QStringRef fileNames, QStringRef *fileName)
    if (!fileNames.startsWith(QStringLiteral("a/")))
    if (!fileNames.mid(fileNameSize).startsWith(" b/"))
    const QStringRef leftFileName = fileNames.mid(2, fileNameSize - 2);
    const QStringRef rightFileName = fileNames.mid(fileNameSize + 3, fileNameSize - 2);
static bool detectFileData(QStringRef patch,
                           FileData *fileData,
                           QStringRef *remainingPatch) {
    QStringRef afterDiffGit;
    const QStringRef diffGit = readLine(patch, &afterDiffGit, &hasNewLine);
    const QStringRef fileNames = diffGit.mid(gitHeader.size());
    QStringRef commonFileName;
        QStringRef afterSecondLine;
        const QStringRef secondLine = readLine(afterDiffGit, &afterSecondLine, &hasNewLine);
            QStringRef afterThirdLine;
        QStringRef afterSimilarity;
        QStringRef afterCopyRenameFrom;
        const QStringRef copyRenameFrom = readLine(afterSimilarity, &afterCopyRenameFrom, &hasNewLine);
        QStringRef afterCopyRenameTo;
        const QStringRef copyRenameTo = readLine(afterCopyRenameFrom, &afterCopyRenameTo, &hasNewLine);
static QList<FileData> readGitPatch(QStringRef patch, bool *ok,
                                    QFutureInterfaceBase *jobController)
        QStringRef patch;
    const int count = startingPositions.count();
                // drop the newline before the next header start
                ? startingPositions.at(i + 1) - 1
                // drop the possible newline by the end of file
                : (patch.at(patch.count() - 1) == newLine ? patch.count() - 1 : patch.count());
        const QStringRef fileDiff = patch.mid(diffStart, diffEnd - diffStart);
        QStringRef remainingFileDiff;
        jobController->setProgressRange(0, patches.count());
    QStringRef croppedPatch(&patch);