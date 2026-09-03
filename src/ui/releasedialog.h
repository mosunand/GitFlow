#pragma once
#include <QDialog>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QListWidget;
class QLabel;
class QPushButton;
class GitHubService;
class GiteeService;

// 独立发布弹窗：仓库 / 标签 / 标题 / 说明 / 正式|预发布 / 附件
class ReleaseDialog : public QDialog {
    Q_OBJECT
public:
    ReleaseDialog(const QString &owner, const QString &repo, const QString &platform,
                  QWidget *parent = nullptr);

private slots:
    void chooseAssets();
    void removeSelectedAsset();
    void publish();

private:
    void setBusy(bool on);
    void precheckReleases();               // 查询 Tag 是否已有 Release
    void doCreate();                       // 预检通过后真正发起创建
    void uploadNext(qint64 releaseId, const QString &htmlUrl);
    void finishOk(const QString &htmlUrl);
    void finishErr(const QString &err);
    void addAssetFile(const QString &path);   // 添加单个附件（去重），拖拽与选择共用

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

    QString m_owner, m_repo, m_platform;
    GitHubService *m_gh = nullptr;
    GiteeService *m_gitee = nullptr;

    QLineEdit *m_tagEdit = nullptr, *m_titleEdit = nullptr;
    QPlainTextEdit *m_bodyEdit = nullptr;
    QRadioButton *m_stableRadio = nullptr, *m_preRadio = nullptr;
    QListWidget *m_assetList = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_publishBtn = nullptr, *m_cancelBtn = nullptr;
    QStringList m_pendingAssets;
    int m_uploadIdx = 0;
    QString m_targetBranch;                // 仓库默认分支（Gitee 创建 Release 必传）
};
