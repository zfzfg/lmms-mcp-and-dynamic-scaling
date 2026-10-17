/*
 * BundleTransaction.cpp - journaled bundle publication under a process lock
 * Copyright (c) 2026 LMMS Developers
 * This file is part of LMMS - https://lmms.io
 */
#include "BundleTransaction.h"
#include "ProjectCodec.h"
#include "RecoveryIdentity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUuid>
#include <QXmlStreamReader>

namespace lmms
{
namespace
{
BundleIoHooks* g_hooks = nullptr;
bool hooked(BundleIoHooks::Step step) { return g_hooks && g_hooks->fail(step); }

QString targetPath(const QString& path)
{
    const QFileInfo info(path);
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parent.isEmpty() || info.fileName().isEmpty() || info.fileName() == "." || info.fileName() == ".." || info.isSymLink()) { return {}; }
    return QDir(parent).filePath(info.fileName());
}
bool regular(const QString& path)
{
    const QFileInfo info(path);
    return info.isFile() && !info.isSymLink();
}
bool directory(const QString& path)
{
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink();
}
bool simpleName(const QString& name)
{
    return !name.isEmpty() && name != "." && name != ".."
        && !name.contains('/') && !name.contains('\\') && !name.contains(':') && !name.contains('\n') && !name.contains('\r');
}
QString manifestProject(const QString& bundle)
{
    if (!directory(bundle) || !regular(bundle + "/.lmms-bundle")) { return {}; }
    QFile file(bundle + "/.lmms-bundle");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) { return {}; }
    const auto lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    if (lines.size() != 2 || lines[0] != "version=1" || !lines[1].startsWith("project=")) { return {}; }
    const QString name = lines[1].mid(8);
    return simpleName(name) && regular(bundle + '/' + name) && directory(bundle + "/resources") ? name : QString();
}
bool validateProject(const QString& bundle, const QString& project, QString* error)
{
    if (!simpleName(project) || !regular(bundle + '/' + project)) { return false; }
    QFile file(bundle + '/' + project);
    const auto limits = ProjectCodec::limits();
    if (!file.open(QIODevice::ReadOnly) || file.size() > limits.maxInputBytes) { return false; }
    auto xml = file.read(limits.maxInputBytes + 1);
    if (ProjectCodec::classify(xml) == ProjectBytesKind::QtCompressed)
    {
        auto decoded = ProjectCodec::decodeQtCompressed(xml);
        if (decoded.status != ProjectCodecStatus::Ok) { if (error) { *error = decoded.message; } return false; }
        xml = decoded.bytes;
    }
    auto inspected = ProjectCodec::inspectXml(xml);
    if (inspected.status != ProjectCodecStatus::Ok) { if (error) { *error = inspected.message; } return false; }
    QXmlStreamReader reader(xml);
    bool root = false;
    while (!reader.atEnd())
    {
        if (reader.readNext() != QXmlStreamReader::StartElement) { continue; }
        if (!root)
        {
            if (reader.name() != QStringLiteral("lmms-project") && reader.name() != QStringLiteral("multimedia-project")) { return false; }
            root = true;
        }
        for (const auto& attribute : reader.attributes())
        {
            const QString value = attribute.value().toString();
            if ((reader.name() == QStringLiteral("sampleclip") || reader.name() == QStringLiteral("audiofileprocessor"))
                && attribute.name() == QStringLiteral("src") && !value.isEmpty() && !value.startsWith("local:resources/"))
            {
                if (error) { *error = QStringLiteral("Resource was not copied into the bundle: %1").arg(value); }
                return false;
            }
            if (!value.startsWith("local:")) { continue; }
            const QString name = value.mid(QStringLiteral("local:resources/").size());
            if (!value.startsWith("local:resources/") || !simpleName(name)
                || !regular(bundle + "/resources/" + name)
                || !directory(bundle + "/resources"))
            {
                if (error) { *error = QStringLiteral("Invalid bundled resource: %1").arg(value); }
                return false;
            }
        }
    }
    return root && !reader.hasError();
}
bool identified(const QString& bundle, const QString& project)
{
    return manifestProject(bundle) == project && !project.isEmpty() && validateProject(bundle, project, nullptr);
}

struct Journal
{
    QString target, staging, backup, project, id, state;
    ProcessIdentity owner;
};
QString journalPath(const QString& target) { return target + ".bundle-transaction.json"; }
bool writeJournal(const Journal& j)
{
    QJsonObject object{{"version", 1}, {"target", j.target}, {"staging", j.staging},
        {"backup", j.backup}, {"project", j.project}, {"id", j.id}, {"state", j.state},
        {"pid", QString::number(j.owner.pid)}, {"start", QString::number(j.owner.startToken)}};
    QSaveFile file(journalPath(j.target));
    file.setDirectWriteFallback(false);
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool readJournal(const QString& target, Journal* j)
{
    QFile file(journalPath(target));
    if (!regular(file.fileName()) || !file.open(QIODevice::ReadOnly) || file.size() > 16384) { return false; }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    j->target = object.value("target").toString(); j->staging = object.value("staging").toString();
    j->backup = object.value("backup").toString(); j->project = object.value("project").toString();
    j->id = object.value("id").toString(); j->state = object.value("state").toString();
    j->owner.pid = object.value("pid").toString().toLongLong();
    j->owner.startToken = object.value("start").toString().toULongLong();
    return object.value("version").toInt() == 1 && j->target == target
        && QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(j->id).hasMatch()
        && j->staging == target + ".staging-" + j->id
        && (j->backup.isEmpty() || j->backup == target + ".bundle-backup-" + j->id)
        && simpleName(j->project)
        && (j->state == "Prepared" || j->state == "OldMoved" || j->state == "NewPublished" || j->state == "CleanupComplete");
}
bool removeTree(const QString& path)
{
    return !QFileInfo::exists(path) || (directory(path) && QDir(path).removeRecursively());
}
// Caller holds the same target lock used for publication. Ambiguous trees stay intact.
bool recoverLocked(const QString& target)
{
    if (!QFileInfo::exists(journalPath(target))) { return true; }
    Journal j;
    if (!readJournal(target, &j)) { return false; }
    const auto self = RecoveryIdentity::currentProcess();
    const bool ours = j.owner.pid == self.pid && j.owner.startToken == self.startToken;
    if (!ours && RecoveryIdentity::inspect(j.owner) != ProcessLiveness::Dead) { return false; }
    const bool staged = QFileInfo::exists(j.staging);
    const bool backed = !j.backup.isEmpty() && QFileInfo::exists(j.backup);
    if (!QFileInfo::exists(target) && backed)
    {
        if (!identified(j.backup, j.project) || !QDir().rename(j.backup, target)) { return false; }
        // Restore the old committed version; a prepared replacement is not committed.
        if (!removeTree(j.staging)) { return false; }
    }
    else if (identified(target, j.project) && !staged)
    {
        if (backed && (!identified(j.backup, j.project) || !removeTree(j.backup))) { return false; }
    }
    else if (!backed && (j.state == "Prepared"
        || (j.state == "OldMoved" && identified(target, j.project))))
    {
        if (!removeTree(j.staging)) { return false; }
    }
    else { return false; }
    return QFile::remove(journalPath(target));
}
BundlePublishResult failure(const QString& code, const QString& message, const Journal& j)
{
    BundlePublishResult result;
    result.code = code; result.message = message; result.publishedPath = j.target;
    if (QFileInfo::exists(j.staging)) { result.stagingPath = j.staging; }
    if (QFileInfo::exists(j.backup)) { result.backupPath = j.backup; }
    if (QFileInfo::exists(journalPath(j.target))) { result.journalPath = journalPath(j.target); }
    return result;
}
} // namespace

void BundleTransaction::setHooks(BundleIoHooks* hooks) { g_hooks = hooks; }
bool BundleTransaction::isIdentifiedBundle(const QString& bundle, const QString& project)
{
    return identified(bundle, project);
}
void BundleTransaction::recoverAbandoned(const QString& parentPath)
{
    QDir parent(parentPath);
    for (const auto& entry : parent.entryInfoList({"*.bundle-transaction.json"}, QDir::Files))
    {
        QString target = entry.absoluteFilePath();
        target.chop(QStringLiteral(".bundle-transaction.json").size());
        if (targetPath(target) != target) { continue; }
        QLockFile lock(target + ".publish-lock");
        lock.setStaleLockTime(0);
        if (lock.tryLock(0)) { recoverLocked(target); }
    }
}
BundlePublishResult BundleTransaction::publish(const QString& bundle, const QString& project,
    const CopyResources& copyResources, const WriteProject& writeProject)
{
    Journal j;
    j.target = targetPath(bundle);
    if (j.target.isEmpty() || !simpleName(project))
    {
        return failure("save.bundle.path", "Invalid bundle path or project name.", j);
    }
    QLockFile lock(j.target + ".publish-lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { return failure("save.bundle.busy", "Another publish owns this target, or its owner cannot be determined.", j); }
    if (!recoverLocked(j.target)) { return failure("save.bundle.recovery", "An unresolved bundle transaction was preserved at " + journalPath(j.target), j); }
    if (QFileInfo::exists(j.target) && !identified(j.target, project))
    {
        return failure("save.bundle.foreign", "The existing folder has no valid LMMS bundle manifest. Choose a new export destination.", j);
    }
    j.project = project; j.id = QUuid::createUuid().toString(QUuid::Id128);
    j.staging = j.target + ".staging-" + j.id;
    j.backup = QFileInfo::exists(j.target) ? j.target + ".bundle-backup-" + j.id : QString();
    j.owner = RecoveryIdentity::currentProcess(); j.state = "Prepared";
    if (!writeJournal(j)) { return failure("save.bundle.journal", "Could not write the bundle journal.", j); }
    auto abortPrepared = [&](const QString& code, const QString& message) {
        if (removeTree(j.staging)) { QFile::remove(journalPath(j.target)); }
        return failure(code, message, j);
    };
    if (hooked(BundleIoHooks::Step::CreateStaging) || !QDir().mkpath(j.staging + "/resources"))
    {
        return abortPrepared("save.bundle.stage", "Could not create bundle staging.");
    }
    QString error;
    if (hooked(BundleIoHooks::Step::CopyResources) || !copyResources || !copyResources(j.staging + "/resources", &error))
    {
        return abortPrepared("save.bundle.resources", "Could not copy bundle resources. " + error);
    }
    if (hooked(BundleIoHooks::Step::WriteProject) || !writeProject || !writeProject(j.staging + '/' + project, &error)
        || !validateProject(j.staging, project, &error))
    {
        return abortPrepared("save.bundle.verify", "Could not write and verify the staged project. " + error);
    }
    QSaveFile manifest(j.staging + "/.lmms-bundle");
    manifest.setDirectWriteFallback(false);
    const QByteArray body = QStringLiteral("version=1\nproject=%1\n").arg(project).toUtf8();
    if (hooked(BundleIoHooks::Step::WriteManifest) || !manifest.open(QIODevice::WriteOnly)
        || manifest.write(body) != body.size() || !manifest.commit())
    {
        manifest.cancelWriting();
        return abortPrepared("save.bundle.manifest", "Could not commit the bundle manifest.");
    }
    if (!identified(j.staging, project)) { return abortPrepared("save.bundle.verify", "The staged bundle did not validate."); }
    hooked(BundleIoHooks::Step::PreparedCheckpoint);
    if (!j.backup.isEmpty())
    {
        // Persist intent first. Recovery reconciles the journal with the actual trees.
        j.state = "OldMoved";
        if (!writeJournal(j)) { return abortPrepared("save.bundle.journal", "Could not record replacement intent."); }
        hooked(BundleIoHooks::Step::OldMoveIntentCheckpoint);
        if (hooked(BundleIoHooks::Step::MoveOld) || !QDir().rename(j.target, j.backup))
        {
            return abortPrepared("save.bundle.move", "Could not move the old bundle; it was left unchanged.");
        }
        hooked(BundleIoHooks::Step::OldMovedCheckpoint);
    }
    if (hooked(BundleIoHooks::Step::PublishNew) || !QDir().rename(j.staging, j.target))
    {
        auto result = failure("save.bundle.publish", "Publication failed. Preserved staging, journal and previous bundle.", j);
        if (!j.backup.isEmpty())
        {
            result.restored = !hooked(BundleIoHooks::Step::RollbackOld) && QDir().rename(j.backup, j.target);
            if (result.restored && removeTree(j.staging)) { QFile::remove(journalPath(j.target)); }
        }
        return result;
    }
    BundlePublishResult result;
    result.ok = true; result.code = "ok"; result.publishedPath = j.target;
    j.state = "NewPublished";
    bool clean = writeJournal(j);
    hooked(BundleIoHooks::Step::NewPublishedCheckpoint);
    if (clean && !j.backup.isEmpty())
    {
        clean = !hooked(BundleIoHooks::Step::CleanupOld) && identified(j.backup, project) && removeTree(j.backup);
    }
    if (clean) { j.state = "CleanupComplete"; clean = writeJournal(j); }
    hooked(BundleIoHooks::Step::CleanupCheckpoint);
    if (clean) { clean = QFile::remove(journalPath(j.target)); }
    if (!clean)
    {
        result.cleanupWarning = true;
        if (QFileInfo::exists(j.backup)) { result.backupPath = j.backup; }
        result.journalPath = journalPath(j.target);
        result.message = "Published bundle. Cleanup artifacts were preserved; journal: " + journalPath(j.target);
    }
    return result;
}
} // namespace lmms
