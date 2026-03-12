/**
 * gui/main.cpp — Qt GUI for the Video Transcoding Pipeline
 *
 * Maps 1-to-1 with every PipelineConfig field.
 * Runs Pipeline::run() in a background QThread so the UI never freezes.
 * Polls PerfMonitor every 500 ms for live stats.
 * Cancel button calls Pipeline::stop() + Pipeline::wait().
 */

#include <transcoder.hpp>
#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QFont>
#include <QTextEdit>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QStatusBar>
#include <QProgressBar>
#include <QString>
#include <QStringList>
#include <QSizePolicy>

#include <memory>
#include <atomic>

using namespace transcoder;

// ─────────────────────────────────────────────────────────────────────────────
// TranscodeWorker
// Runs in a separate QThread. Owns the Pipeline.
// ─────────────────────────────────────────────────────────────────────────────

class TranscodeWorker : public QObject
{
    Q_OBJECT
public:
    void setConfig(const PipelineConfig& cfg) { cfg_ = cfg; }

    PerfMonitor* monitor() const {
        return pipeline_ ? pipeline_->getPerfMonitor() : nullptr;
    }

public slots:
    void start()
    {
        pipeline_ = std::make_unique<Pipeline>(cfg_);

        if (!pipeline_->initialize()) {
            emit finished(false, "Pipeline initialization failed.\nCheck input/output paths and codec compatibility.");
            return;
        }

        bool ok = pipeline_->run();

        if (ok) {
            auto* m = pipeline_->getPerfMonitor();
            QString msg = QString("Encoded %1 frames — %2 MB output")
                .arg(m->totalFramesEncoded())
                .arg(m->totalBytesOutput() / 1024 / 1024);
            emit finished(true, msg);
        } else {
            emit finished(false, "Transcoding failed or was cancelled.");
        }
    }

    void cancel()
    {
        if (pipeline_) {
            pipeline_->stop();
            pipeline_->wait();
        }
    }

signals:
    void finished(bool ok, QString message);

private:
    PipelineConfig                cfg_;
    std::unique_ptr<Pipeline>     pipeline_;
};

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow
// ─────────────────────────────────────────────────────────────────────────────

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Video Transcoding Pipeline");
        setMinimumSize(820, 720);
        resize(960, 800);

        buildUi();
        connectSignals();
        syncBitratePanel();
        syncAudioWidgets();
        syncSliceWidget();
        updatePreview();
    }

    ~MainWindow() {
        if (workerThread_ && workerThread_->isRunning()) {
            if (worker_) worker_->cancel();
            workerThread_->quit();
            workerThread_->wait(3000);
        }
    }

private:

    // ── Config page widgets ───────────────────────────────────────────────────

    // I/O
    QLineEdit*   m_inputPath;
    QPushButton* m_browseInput;
    QLineEdit*   m_outputPath;
    QPushButton* m_browseOutput;
    QLabel*      m_sourceTypeLabel;

    // Video codec
    QCheckBox*   m_keepVideoCodec;
    QComboBox*   m_videoCodecCombo;

    // Audio
    QCheckBox*   m_noAudio;
    QCheckBox*   m_keepAudioCodec;
    QComboBox*   m_audioCodecCombo;
    QWidget*     m_audioCodecRow;

    // Framerate
    QCheckBox*   m_keepFramerate;
    QSpinBox*    m_fpsSpin;

    // Bitrate
    QCheckBox*   m_keepBitrate;
    QComboBox*   m_bitrateModeCombo;
    QWidget*     m_bitrateValuePanel;   // CBR / VBR
    QSpinBox*    m_bitrateSpin;         // kbps
    QWidget*     m_crfPanel;            // CQP
    QSlider*     m_crfSlider;
    QLabel*      m_crfValueLabel;

    // Advanced
    QSpinBox*    m_queueSpin;
    QCheckBox*   m_noStats;
    QSpinBox*    m_statsIntervalSpin;
    QLabel*      m_statsIntervalLabel;

    // Stream slicing
    QGroupBox*   m_streamGroup;
    QCheckBox*   m_segmentEnabled;
    QSpinBox*    m_sliceSpin;

    // CLI preview
    QTextEdit*   m_preview;

    // Start button
    QPushButton* m_startBtn;

    // ── Progress page widgets ─────────────────────────────────────────────────

    QLabel*      m_progInputLabel;
    QLabel*      m_progOutputLabel;
    QLabel*      m_statFrames;
    QLabel*      m_statBytes;
    QLabel*      m_statElapsed;
    QLabel*      m_statFPS;
    QProgressBar* m_progressBar;   // indeterminate spinner
    QPushButton* m_cancelBtn;
    QTextEdit*   m_logBox;

    // ── Stack ─────────────────────────────────────────────────────────────────

    QStackedWidget* m_stack;   // page 0 = config, page 1 = progress

    // ── Runtime ──────────────────────────────────────────────────────────────

    QThread*          workerThread_  = nullptr;
    TranscodeWorker*  worker_        = nullptr;
    QTimer*           pollTimer_     = nullptr;
    QElapsedTimer     elapsedTimer_;
    int64_t           lastEncoded_   = 0;

    // ── Build UI ──────────────────────────────────────────────────────────────

    void buildUi()
    {
        m_stack = new QStackedWidget;
        setCentralWidget(m_stack);

        m_stack->addWidget(buildConfigPage());   // index 0
        m_stack->addWidget(buildProgressPage()); // index 1
        m_stack->setCurrentIndex(0);

        statusBar()->showMessage("Ready");
    }

    // ── Config page ───────────────────────────────────────────────────────────

    QWidget* buildConfigPage()
    {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto* page = new QWidget;
        scroll->setWidget(page);

        auto* root = new QVBoxLayout(page);
        root->setSpacing(10);
        root->setContentsMargins(16, 16, 16, 16);

        // Title
        auto* title = new QLabel("Video Transcoding Pipeline");
        QFont f = title->font(); f.setPointSize(15); f.setBold(true);
        title->setFont(f);
        title->setAlignment(Qt::AlignCenter);
        root->addWidget(title);

        root->addWidget(makeSeparator());
        root->addWidget(buildIOGroup());
        root->addWidget(buildVideoGroup());
        root->addWidget(buildAudioGroup());
        root->addWidget(buildFramerateGroup());
        root->addWidget(buildBitrateGroup());
        root->addWidget(buildAdvancedGroup());
        m_segmentEnabled = new QCheckBox("Enable stream slicing   (RTSP input only)");
        root->addWidget(m_segmentEnabled);
        root->addWidget(buildStreamGroup());
        root->addWidget(makeSeparator());

        // CLI preview
        auto* prevLabel = new QLabel("Equivalent CLI command:");
        prevLabel->setStyleSheet("font-weight:bold;");
        root->addWidget(prevLabel);

        m_preview = new QTextEdit;
        m_preview->setReadOnly(true);
        m_preview->setFixedHeight(72);
        m_preview->setFont(QFont("Courier", 9));
        m_preview->setStyleSheet(
            "background:#1e1e1e; color:#d4d4d4; border-radius:4px; padding:4px;");
        root->addWidget(m_preview);

        // Start button
        m_startBtn = new QPushButton("▶   Start Transcoding");
        m_startBtn->setFixedHeight(46);
        m_startBtn->setStyleSheet(R"(
            QPushButton           { background:#0078d4; color:white; font-size:14px;
                                    font-weight:bold; border-radius:6px; }
            QPushButton:hover     { background:#106ebe; }
            QPushButton:pressed   { background:#005a9e; }
            QPushButton:disabled  { background:#aaa;    }
        )");
        root->addWidget(m_startBtn);

        return scroll;
    }

    QGroupBox* buildIOGroup()
    {
        auto* grp = new QGroupBox("Input / Output   (required)");
        auto* g   = new QGridLayout(grp);
        g->setColumnStretch(1, 1);

        g->addWidget(new QLabel("Input:"), 0, 0, Qt::AlignRight);
        m_inputPath = new QLineEdit;
        m_inputPath->setPlaceholderText(
            "File path  (e.g. /home/user/video.mp4)   or   rtsp://host/stream");
        g->addWidget(m_inputPath, 0, 1);
        m_browseInput = new QPushButton("Browse…");
        g->addWidget(m_browseInput, 0, 2);

        m_sourceTypeLabel = new QLabel();
        m_sourceTypeLabel->setStyleSheet("color:#0078d4; font-style:italic; font-size:11px;");
        g->addWidget(m_sourceTypeLabel, 1, 1);

        g->addWidget(new QLabel("Output:"), 2, 0, Qt::AlignRight);
        m_outputPath = new QLineEdit;
        m_outputPath->setPlaceholderText(
            "File path  (e.g. /home/user/output.mkv)   — .mp4 .mkv .avi .ts .flv .mov");
        g->addWidget(m_outputPath, 2, 1);
        m_browseOutput = new QPushButton("Browse…");
        g->addWidget(m_browseOutput, 2, 2);

        return grp;
    }

    QGroupBox* buildVideoGroup()
    {
        auto* grp = new QGroupBox("Video Codec");
        auto* h   = new QHBoxLayout(grp);

        m_keepVideoCodec = new QCheckBox("Keep original codec");
        m_keepVideoCodec->setChecked(true);
        h->addWidget(m_keepVideoCodec);

        h->addSpacing(20);
        h->addWidget(new QLabel("Target codec:"));

        m_videoCodecCombo = new QComboBox;
        // Must match VideoCodecType enum FIRST..LAST order: H264,H265,MJPEG,MPEG4,VP9,AV1
        m_videoCodecCombo->addItem("H.264",  QVariant::fromValue(int(VideoCodecType::H264)));
        m_videoCodecCombo->addItem("H.265",  QVariant::fromValue(int(VideoCodecType::H265)));
        m_videoCodecCombo->addItem("MJPEG",  QVariant::fromValue(int(VideoCodecType::MJPEG)));
        m_videoCodecCombo->addItem("MPEG-4", QVariant::fromValue(int(VideoCodecType::MPEG4)));
        m_videoCodecCombo->addItem("VP9",    QVariant::fromValue(int(VideoCodecType::VP9)));
        m_videoCodecCombo->addItem("AV1",    QVariant::fromValue(int(VideoCodecType::AV1)));
        m_videoCodecCombo->setEnabled(false);
        h->addWidget(m_videoCodecCombo);
        h->addStretch();
        return grp;
    }

    QGroupBox* buildAudioGroup()
    {
        auto* grp = new QGroupBox("Audio");
        auto* v   = new QVBoxLayout(grp);

        m_noAudio = new QCheckBox("Discard audio stream   (--no-audio)");
        v->addWidget(m_noAudio);

        m_audioCodecRow = new QWidget;
        auto* h = new QHBoxLayout(m_audioCodecRow);
        h->setContentsMargins(0, 0, 0, 0);

        m_keepAudioCodec = new QCheckBox("Keep original audio codec");
        m_keepAudioCodec->setChecked(true);
        h->addWidget(m_keepAudioCodec);

        h->addSpacing(20);
        h->addWidget(new QLabel("Target codec:"));

        m_audioCodecCombo = new QComboBox;
        // Must match AudioCodecType enum FIRST..LAST: AAC,MP3,AC3,EAC3,DTS,OPUS,VORBIS,PCM,FLAC,ALAC,MP2,SPEEX,WMAV2
        m_audioCodecCombo->addItem("AAC",    QVariant::fromValue(int(AudioCodecType::AAC)));
        m_audioCodecCombo->addItem("MP3",    QVariant::fromValue(int(AudioCodecType::MP3)));
        m_audioCodecCombo->addItem("AC3",    QVariant::fromValue(int(AudioCodecType::AC3)));
        m_audioCodecCombo->addItem("E-AC3",  QVariant::fromValue(int(AudioCodecType::EAC3)));
        m_audioCodecCombo->addItem("DTS",    QVariant::fromValue(int(AudioCodecType::DTS)));
        m_audioCodecCombo->addItem("Opus",   QVariant::fromValue(int(AudioCodecType::OPUS)));
        m_audioCodecCombo->addItem("Vorbis", QVariant::fromValue(int(AudioCodecType::VORBIS)));
        m_audioCodecCombo->addItem("PCM",    QVariant::fromValue(int(AudioCodecType::PCM)));
        m_audioCodecCombo->addItem("FLAC",   QVariant::fromValue(int(AudioCodecType::FLAC)));
        m_audioCodecCombo->addItem("ALAC",   QVariant::fromValue(int(AudioCodecType::ALAC)));
        m_audioCodecCombo->addItem("MP2",    QVariant::fromValue(int(AudioCodecType::MP2)));
        m_audioCodecCombo->addItem("Speex",  QVariant::fromValue(int(AudioCodecType::SPEEX)));
        m_audioCodecCombo->addItem("WMA v2", QVariant::fromValue(int(AudioCodecType::WMAV2)));
        m_audioCodecCombo->setEnabled(false);
        h->addWidget(m_audioCodecCombo);
        h->addStretch();

        v->addWidget(m_audioCodecRow);
        return grp;
    }

    QGroupBox* buildFramerateGroup()
    {
        auto* grp = new QGroupBox("Framerate");
        auto* h   = new QHBoxLayout(grp);

        m_keepFramerate = new QCheckBox("Keep original framerate");
        m_keepFramerate->setChecked(true);
        h->addWidget(m_keepFramerate);

        h->addSpacing(20);
        h->addWidget(new QLabel("Target FPS:"));

        m_fpsSpin = new QSpinBox;
        m_fpsSpin->setRange(1, 240);
        m_fpsSpin->setValue(30);
        m_fpsSpin->setSuffix(" fps");
        m_fpsSpin->setEnabled(false);
        h->addWidget(m_fpsSpin);
        h->addStretch();
        return grp;
    }

    QGroupBox* buildBitrateGroup()
    {
        auto* grp = new QGroupBox("Bitrate Control");
        auto* v   = new QVBoxLayout(grp);

        // Row 1: keep toggle + mode selector
        auto* row1 = new QHBoxLayout;
        m_keepBitrate = new QCheckBox("Keep original bitrate");
        m_keepBitrate->setChecked(true);
        row1->addWidget(m_keepBitrate);
        row1->addSpacing(20);
        row1->addWidget(new QLabel("Mode:"));
        m_bitrateModeCombo = new QComboBox;
        m_bitrateModeCombo->addItem("CBR — Constant Bitrate");
        m_bitrateModeCombo->addItem("VBR — Variable Bitrate");
        m_bitrateModeCombo->addItem("CQP — Constant Quality (CRF)");
        m_bitrateModeCombo->setEnabled(false);
        row1->addWidget(m_bitrateModeCombo);
        row1->addStretch();
        v->addLayout(row1);

        // Row 2a: target bitrate value (CBR / VBR)
        m_bitrateValuePanel = new QWidget;
        auto* bh = new QHBoxLayout(m_bitrateValuePanel);
        bh->setContentsMargins(0, 0, 0, 0);
        bh->addWidget(new QLabel("Target bitrate:"));
        m_bitrateSpin = new QSpinBox;
        m_bitrateSpin->setRange(50, 500000);  // kbps
        m_bitrateSpin->setValue(4000);
        m_bitrateSpin->setSuffix(" kbps");
        m_bitrateSpin->setSingleStep(500);
        bh->addWidget(m_bitrateSpin);
        bh->addWidget(new QLabel("(1000 kbps = 1 Mbps)"));
        bh->addStretch();
        v->addWidget(m_bitrateValuePanel);

        // Row 2b: CRF slider (CQP)
        m_crfPanel = new QWidget;
        auto* ch = new QHBoxLayout(m_crfPanel);
        ch->setContentsMargins(0, 0, 0, 0);
        ch->addWidget(new QLabel("CRF  (0 = best quality,  51 = worst):"));
        m_crfSlider = new QSlider(Qt::Horizontal);
        m_crfSlider->setRange(0, 51);
        m_crfSlider->setValue(23);
        m_crfSlider->setTickInterval(5);
        m_crfSlider->setTickPosition(QSlider::TicksBelow);
        ch->addWidget(m_crfSlider);
        m_crfValueLabel = new QLabel("23");
        m_crfValueLabel->setFixedWidth(28);
        m_crfValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ch->addWidget(m_crfValueLabel);
        v->addWidget(m_crfPanel);

        return grp;
    }

    QGroupBox* buildAdvancedGroup()
    {
        auto* grp = new QGroupBox("Advanced");
        auto* g   = new QGridLayout(grp);

        // Queue size
        g->addWidget(new QLabel("Queue size:"), 0, 0, Qt::AlignRight);
        m_queueSpin = new QSpinBox;
        m_queueSpin->setRange(1, 5000);
        m_queueSpin->setValue(300);
        m_queueSpin->setSuffix(" frames");
        g->addWidget(m_queueSpin, 0, 1);
        g->addWidget(new QLabel(
            "<small>Max frames in-flight between decode and encode threads.</small>"),
            0, 2);

        // Stats toggle
        m_noStats = new QCheckBox("Disable live stats   (--no-stats)");
        g->addWidget(m_noStats, 1, 0, 1, 2);

        m_statsIntervalLabel = new QLabel("Stats interval:");
        g->addWidget(m_statsIntervalLabel, 2, 0, Qt::AlignRight);
        m_statsIntervalSpin = new QSpinBox;
        m_statsIntervalSpin->setRange(1, 300);
        m_statsIntervalSpin->setValue(1);
        m_statsIntervalSpin->setSuffix(" s");
        g->addWidget(m_statsIntervalSpin, 2, 1);

        g->setColumnStretch(3, 1);
        return grp;
    }

    QGroupBox* buildStreamGroup()
    {
        // m_streamGroup = new QGroupBox("Stream Slicing   (RTSP input only)");
        m_segmentEnabled = new QCheckBox("Enable stream slicing"); 
        auto* h = new QHBoxLayout(m_streamGroup);
        h->addWidget(m_segmentEnabled);
        h->addWidget(new QLabel("Slice interval:"));
        m_sliceSpin = new QSpinBox;
        m_sliceSpin->setRange(1, 3600);
        m_sliceSpin->setValue(30);
        m_sliceSpin->setSuffix(" s");
        h->addWidget(m_sliceSpin);
        h->addWidget(new QLabel(
            "<small>Each output file will contain this many seconds of stream.</small>"));
        h->addStretch();

        m_streamGroup->setVisible(false);
        return m_streamGroup;
    }

    // ── Progress page ─────────────────────────────────────────────────────────

    QWidget* buildProgressPage()
    {
        auto* page = new QWidget;
        auto* root = new QVBoxLayout(page);
        root->setSpacing(16);
        root->setContentsMargins(24, 24, 24, 24);

        // Title
        auto* title = new QLabel("Transcoding in progress…");
        QFont f = title->font(); f.setPointSize(14); f.setBold(true);
        title->setFont(f);
        title->setAlignment(Qt::AlignCenter);
        root->addWidget(title);

        // I/O labels
        auto* ioBox = new QGroupBox("Job");
        auto* ioGrid = new QGridLayout(ioBox);
        ioGrid->addWidget(new QLabel("Input:"),  0, 0, Qt::AlignRight);
        m_progInputLabel  = new QLabel("—");
        m_progInputLabel->setWordWrap(true);
        ioGrid->addWidget(m_progInputLabel,  0, 1);
        ioGrid->addWidget(new QLabel("Output:"), 1, 0, Qt::AlignRight);
        m_progOutputLabel = new QLabel("—");
        m_progOutputLabel->setWordWrap(true);
        ioGrid->addWidget(m_progOutputLabel, 1, 1);
        ioGrid->setColumnStretch(1, 1);
        root->addWidget(ioBox);

        // Indeterminate progress bar
        m_progressBar = new QProgressBar;
        m_progressBar->setRange(0, 0);   // indeterminate
        m_progressBar->setFixedHeight(20);
        root->addWidget(m_progressBar);

        // Live stats
        auto* statsBox = new QGroupBox("Live Statistics");
        auto* sg = new QGridLayout(statsBox);

        auto makeStat = [&](const QString& label, QLabel*& out, int row) {
            sg->addWidget(new QLabel(label), row, 0, Qt::AlignRight);
            out = new QLabel("—");
            QFont sf = out->font(); sf.setPointSize(13); sf.setBold(true);
            out->setFont(sf);
            sg->addWidget(out, row, 1);
        };

        makeStat("Frames encoded:",  m_statFrames,  0);
        makeStat("Output size:",      m_statBytes,   1);
        makeStat("Elapsed time:",     m_statElapsed, 2);
        makeStat("Encoding speed:",   m_statFPS,     3);

        sg->setColumnStretch(2, 1);
        root->addWidget(statsBox);

        // Log / status messages
        auto* logLabel = new QLabel("Log:");
        logLabel->setStyleSheet("font-weight:bold;");
        root->addWidget(logLabel);

        m_logBox = new QTextEdit;
        m_logBox->setReadOnly(true);
        m_logBox->setFont(QFont("Courier", 9));
        m_logBox->setMaximumHeight(120);
        m_logBox->setStyleSheet(
            "background:#1e1e1e; color:#d4d4d4; border-radius:4px; padding:4px;");
        root->addWidget(m_logBox);

        root->addStretch();

        // Cancel button
        m_cancelBtn = new QPushButton("■   Cancel Transcoding");
        m_cancelBtn->setFixedHeight(44);
        m_cancelBtn->setStyleSheet(R"(
            QPushButton          { background:#c42b1c; color:white; font-size:14px;
                                   font-weight:bold; border-radius:6px; }
            QPushButton:hover    { background:#a52314; }
            QPushButton:pressed  { background:#8a1e10; }
            QPushButton:disabled { background:#aaa;    }
        )");
        root->addWidget(m_cancelBtn);

        return page;
    }

    // ── Signals ───────────────────────────────────────────────────────────────

    void connectSignals()
    {
        // Browse
        connect(m_browseInput,  &QPushButton::clicked, this, &MainWindow::onBrowseInput);
        connect(m_browseOutput, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);

        // Input path → detect source type, toggle slice group
        connect(m_inputPath, &QLineEdit::textChanged, this, [this](const QString& t){
            bool isRtsp = t.toLower().startsWith("rtsp://") ||
                          t.toLower().startsWith("rtsps://");
            m_sourceTypeLabel->setText(isRtsp ? "⟶ RTSP Stream detected"
                                               : t.isEmpty() ? "" : "⟶ File");
            syncSliceWidget();
            updatePreview();
        });

        // Video codec keep toggle
        connect(m_keepVideoCodec, &QCheckBox::toggled, this, [this](bool keep){
            m_videoCodecCombo->setEnabled(!keep);
            updatePreview();
        });

        // Audio widgets
        connect(m_noAudio, &QCheckBox::toggled, this, [this]{
            syncAudioWidgets();
            updatePreview();
        });
        connect(m_keepAudioCodec, &QCheckBox::toggled, this, [this](bool keep){
            m_audioCodecCombo->setEnabled(!keep && !m_noAudio->isChecked());
            updatePreview();
        });

        // Framerate
        connect(m_keepFramerate, &QCheckBox::toggled, this, [this](bool keep){
            m_fpsSpin->setEnabled(!keep);
            updatePreview();
        });

        // Bitrate mode
        connect(m_keepBitrate, &QCheckBox::toggled, this, [this]{
            syncBitratePanel();
            updatePreview();
        });
        connect(m_bitrateModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this]{ syncBitratePanel(); updatePreview(); });

        // CRF slider → label
        connect(m_crfSlider, &QSlider::valueChanged, this, [this](int v){
            m_crfValueLabel->setText(QString::number(v));
            updatePreview();
        });

        // Stats toggle
        connect(m_noStats, &QCheckBox::toggled, this, [this](bool disabled){
            m_statsIntervalSpin->setEnabled(!disabled);
            m_statsIntervalLabel->setEnabled(!disabled);
            updatePreview();
        });

        connect(m_segmentEnabled,&QCheckBox::toggled,this,[this](bool enabled) {
            m_streamGroup -> setEnabled(enabled);
        });

        // Any value change → refresh preview
        auto upd = [this]{ updatePreview(); };
        connect(m_outputPath,        &QLineEdit::textChanged,                           this, upd);
        connect(m_videoCodecCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, upd);
        connect(m_audioCodecCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, upd);
        connect(m_fpsSpin,           QOverload<int>::of(&QSpinBox::valueChanged),       this, upd);
        connect(m_bitrateSpin,       QOverload<int>::of(&QSpinBox::valueChanged),       this, upd);
        connect(m_queueSpin,         QOverload<int>::of(&QSpinBox::valueChanged),       this, upd);
        connect(m_statsIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),       this, upd);
        connect(m_sliceSpin,         QOverload<int>::of(&QSpinBox::valueChanged),       this, upd);

        // Start / Cancel
        connect(m_startBtn,  &QPushButton::clicked, this, &MainWindow::onStart);
        connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    }

    // ── Sync helpers ──────────────────────────────────────────────────────────

    void syncBitratePanel()
    {
        bool keep  = m_keepBitrate->isChecked();
        m_bitrateModeCombo->setEnabled(!keep);
        bool isCqp = (m_bitrateModeCombo->currentIndex() == 2);
        m_bitrateValuePanel->setVisible(!keep && !isCqp);
        m_crfPanel->setVisible(!keep && isCqp);
    }

    void syncAudioWidgets()
    {
        bool noAudio = m_noAudio->isChecked();
        m_audioCodecRow->setEnabled(!noAudio);
        m_audioCodecCombo->setEnabled(!noAudio && !m_keepAudioCodec->isChecked());
    }

    void syncSliceWidget()
    {
        // bool isRtsp = m_inputPath->text().toLower().startsWith("rtsp://") ||
        //               m_inputPath->text().toLower().startsWith("rtsps://");
        bool isSegment = m_segmentEnabled->isChecked();
        m_streamGroup->setVisible(isSegment);
    }

    // ── CLI preview ───────────────────────────────────────────────────────────

    void updatePreview()
    {
        QString inp = m_inputPath->text().trimmed();
        QString out = m_outputPath->text().trimmed();

        QString cmd = "transcode_cli";
        cmd += " -i " + (inp.isEmpty() ? "<input>" : shellQuote(inp));
        cmd += " -o " + (out.isEmpty() ? "<output>" : shellQuote(out));

        if (!m_keepVideoCodec->isChecked())
            cmd += " --video-codec " + m_videoCodecCombo->currentText().toLower()
                                                          .remove(".")
                                                          .remove("-");

        if (!m_noAudio->isChecked() && !m_keepAudioCodec->isChecked())
            cmd += " --audio-codec " + m_audioCodecCombo->currentText().toLower()
                                                           .remove(" v2");

        if (!m_keepFramerate->isChecked())
            cmd += " --fps " + QString::number(m_fpsSpin->value());

        if (!m_keepBitrate->isChecked()) {
            int idx = m_bitrateModeCombo->currentIndex();
            cmd += " --bitrate-mode " + QStringList{"cbr","vbr","cqp"}[idx];
            if (idx == 2)
                cmd += " --crf " + QString::number(m_crfSlider->value());
            else
                cmd += " --bitrate " + QString::number(m_bitrateSpin->value() * 1000);
        }

        if (m_queueSpin->value() != 300)
            cmd += " --queue-size " + QString::number(m_queueSpin->value());

        if (m_noAudio->isChecked())
            cmd += " --no-audio";

        if (m_noStats->isChecked())
            cmd += " --no-stats";
        else if (m_statsIntervalSpin->value() != 1)
            cmd += " --stats-interval " + QString::number(m_statsIntervalSpin->value());

        bool isRtsp = inp.toLower().startsWith("rtsp://");
        if (isRtsp && m_sliceSpin->value() != 30)
            cmd += " --slice-interval " + QString::number(m_sliceSpin->value());

        m_preview->setPlainText(cmd);
    }

    // ── Build PipelineConfig from form — every field explicitly set ───────────

    PipelineConfig buildConfig() const
    {
        PipelineConfig cfg;

        // ── Input ─────────────────────────────────────────────────────────────
        cfg.inputPath  = m_inputPath->text().trimmed().toStdString();
        bool isRtsp    = m_inputPath->text().trimmed().toLower().startsWith("rtsp://") ||
                         m_inputPath->text().trimmed().toLower().startsWith("rtsps://");
        cfg.sourceType = isRtsp ? SourceType::RTSP : SourceType::FILE;

        // ── Output ────────────────────────────────────────────────────────────
        cfg.outputPath = m_outputPath->text().trimmed().toStdString();

        // ── Video codec ───────────────────────────────────────────────────────
        cfg.keepOriginalVideoCodec = m_keepVideoCodec->isChecked();
        cfg.targetVideoCodec = static_cast<VideoCodecType>(
            m_videoCodecCombo->currentData().toInt());

        // ── Audio codec ───────────────────────────────────────────────────────
        cfg.passAudio = !m_noAudio->isChecked();
        cfg.keepOriginalAudioCodec = m_keepAudioCodec->isChecked();
        cfg.targetAudioCodec = static_cast<AudioCodecType>(
            m_audioCodecCombo->currentData().toInt());

        // ── Framerate ─────────────────────────────────────────────────────────
        cfg.keepOriginalFramerate = m_keepFramerate->isChecked();
        cfg.targetFramerate       = m_fpsSpin->value();

        // ── Bitrate ───────────────────────────────────────────────────────────
        cfg.keepOriginalBitrate = m_keepBitrate->isChecked();
        int modeIdx = m_bitrateModeCombo->currentIndex();
        cfg.bitrateMode   = (modeIdx == 0) ? BitrateMode::CBR
                          : (modeIdx == 1) ? BitrateMode::VBR
                                           : BitrateMode::CQP;
        cfg.targetBitrate = m_bitrateSpin->value() * 1000;  // kbps → bps
        cfg.crf           = m_crfSlider->value();

        // ── Queue ─────────────────────────────────────────────────────────────
        cfg.queueMaxSize = m_queueSpin->value();

        // ── Stats ─────────────────────────────────────────────────────────────
        cfg.enableStats    = !m_noStats->isChecked();
        cfg.statsInterval  = m_statsIntervalSpin->value();

        // ── Stream slicing ────────────────────────────────────────────────────
        cfg.segmentDurationSeconds = m_sliceSpin->value();

        // ── Processing stage (off for now) ────────────────────────────────────
        cfg.enableProcessing = false;

        return cfg;
    }

    // ── Start transcoding ─────────────────────────────────────────────────────

    void onStart()
    {
        // ── Form validation ───────────────────────────────────────────────────
        QStringList errs;

        if (m_inputPath->text().trimmed().isEmpty())
            errs << "• Input path is required.";

        if (m_outputPath->text().trimmed().isEmpty()) {
            errs << "• Output path is required.";
        } else {
            static const QStringList valid = {".mp4",".mkv",".avi",".ts",".flv",".mov"};
            QString lo = m_outputPath->text().trimmed().toLower();
            bool ok = false;
            for (auto& e : valid) if (lo.endsWith(e)) { ok = true; break; }
            if (!ok) errs << "• Output must end in .mp4  .mkv  .avi  .ts  .flv  .mov";
        }

        if (!m_keepBitrate->isChecked()) {
            int idx = m_bitrateModeCombo->currentIndex();
            if (idx == 0 || idx == 1) {
                if (m_bitrateSpin->value() <= 0)
                    errs << "• CBR/VBR requires a positive target bitrate.";
            }
        }

        if (!m_keepFramerate->isChecked() && m_fpsSpin->value() <= 0)
            errs << "• Target framerate must be positive.";

        if (!errs.isEmpty()) {
            QMessageBox::warning(this, "Cannot Start", errs.join("\n"));
            return;
        }

        // ── Build config ──────────────────────────────────────────────────────
        PipelineConfig cfg = buildConfig();

        // ── Run ConfigValidator (the library's own validator) ─────────────────
        auto result = ConfigValidator::validate(cfg);
        if (!result.ok()) {
            QMessageBox::critical(this, "Configuration Error",
                QString::fromStdString(result.summary()));
            return;
        }
        if (result.hasWarnings()) {
            auto btn = QMessageBox::warning(this, "Warnings",
                QString::fromStdString(result.summary()) +
                "\nProceed anyway?",
                QMessageBox::Yes | QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
        }

        // ── Switch to progress page ───────────────────────────────────────────
        m_progInputLabel->setText(m_inputPath->text().trimmed());
        m_progOutputLabel->setText(m_outputPath->text().trimmed());
        m_logBox->clear();
        m_logBox->append("Initializing pipeline…");
        m_statFrames->setText("—");
        m_statBytes->setText("—");
        m_statElapsed->setText("—");
        m_statFPS->setText("—");
        m_progressBar->setRange(0, 0);  // indeterminate
        m_cancelBtn->setEnabled(true);
        m_stack->setCurrentIndex(1);
        statusBar()->showMessage("Transcoding…");

        // ── Spin up worker thread ─────────────────────────────────────────────
        workerThread_ = new QThread(this);
        worker_       = new TranscodeWorker;
        worker_->setConfig(cfg);
        worker_->moveToThread(workerThread_);

        connect(workerThread_, &QThread::started,   worker_, &TranscodeWorker::start);
        connect(worker_, &TranscodeWorker::finished, this,  &MainWindow::onTranscodeFinished);
        connect(worker_, &TranscodeWorker::finished, workerThread_, &QThread::quit);
        connect(workerThread_, &QThread::finished,  worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished,  workerThread_, &QObject::deleteLater);

        // Poll stats every 500 ms
        pollTimer_ = new QTimer(this);
        pollTimer_->setInterval(500);
        connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollStats);

        elapsedTimer_.start();
        lastEncoded_ = 0;
        workerThread_->start();
        pollTimer_->start();

        m_logBox->append("Pipeline running…");
    }

    // ── Cancel ────────────────────────────────────────────────────────────────

    void onCancel()
    {
        m_cancelBtn->setEnabled(false);
        m_logBox->append("Cancelling — waiting for pipeline to stop…");
        statusBar()->showMessage("Cancelling…");
        if (worker_) {
            // Cancel is called from main thread; worker lives in workerThread_
            // Use QMetaObject::invokeMethod to cross threads safely
            QMetaObject::invokeMethod(worker_, "cancel", Qt::QueuedConnection);
        }
    }

    // ── Poll stats every 500 ms ───────────────────────────────────────────────

    void pollStats()
    {
        if (!worker_) return;
        PerfMonitor* m = worker_->monitor();
        if (!m) return;

        int64_t encoded = m->totalFramesEncoded();
        int64_t bytes   = m->totalBytesOutput();
        qint64  msElapsed = elapsedTimer_.elapsed();

        m_statFrames->setText(QString::number(encoded) + " frames");
        m_statBytes->setText(
            bytes < 1024*1024
            ? QString::number(bytes / 1024) + " KB"
            : QString::number(bytes / 1024 / 1024) + " MB");

        qint64 sec = msElapsed / 1000;
        m_statElapsed->setText(QString("%1:%2")
            .arg(sec / 60, 2, 10, QChar('0'))
            .arg(sec % 60, 2, 10, QChar('0')));

        // FPS over last half-second
        int64_t delta = encoded - lastEncoded_;
        double fps = delta * 2.0;  // 500 ms period
        lastEncoded_ = encoded;
        m_statFPS->setText(QString::number(fps, 'f', 1) + " fps");
    }

    // ── Transcoding finished ──────────────────────────────────────────────────

    void onTranscodeFinished(bool ok, QString message)
    {
        pollTimer_->stop();
        pollTimer_->deleteLater();
        pollTimer_ = nullptr;

        // Do one final stats read
        if (worker_) {
            PerfMonitor* m = worker_->monitor();
            if (m) {
                m_statFrames->setText(QString::number(m->totalFramesEncoded()) + " frames");
                int64_t bytes = m->totalBytesOutput();
                m_statBytes->setText(
                    bytes < 1024*1024
                    ? QString::number(bytes / 1024) + " KB"
                    : QString::number(bytes / 1024 / 1024) + " MB");
            }
        }

        // Stop indeterminate bar
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(ok ? 1 : 0);
        m_cancelBtn->setEnabled(false);

        if (ok) {
            m_logBox->append("\n✓ " + message);
            statusBar()->showMessage("Done — " + message);
            QMessageBox::information(this, "Transcoding Complete",
                message + "\n\nOutput: " + m_outputPath->text().trimmed());
        } else {
            m_logBox->append("\n✗ " + message);
            statusBar()->showMessage("Failed — " + message);
            QMessageBox::critical(this, "Transcoding Failed", message);
        }

        worker_       = nullptr;
        workerThread_ = nullptr;

        // Add a "Back" button behaviour — re-show config page
        auto* backBtn = new QPushButton("← Back to Configuration");
        backBtn->setFixedHeight(40);
        connect(backBtn, &QPushButton::clicked, this, [this, backBtn]{
            m_stack->setCurrentIndex(0);
            statusBar()->showMessage("Ready");
            backBtn->deleteLater();
        });
        // Insert above cancel button (last widget in progress page layout)
        auto* layout = qobject_cast<QVBoxLayout*>(
            m_stack->widget(1)->layout());
        if (layout)
            layout->insertWidget(layout->count() - 1, backBtn);
    }

    // ── Browse slots ──────────────────────────────────────────────────────────

    void onBrowseInput()
    {
        QString path = QFileDialog::getOpenFileName(
            this, "Select Input Video",
            QString(),
            "Video files (*.mp4 *.mkv *.avi *.ts *.flv *.mov *.m2ts);;All files (*)");
        if (!path.isEmpty()) m_inputPath->setText(path);
    }

    void onBrowseOutput()
    {
        QString path = QFileDialog::getSaveFileName(
            this, "Select Output File",
            QString(),
            "MP4 (*.mp4);;MKV (*.mkv);;AVI (*.avi);;MPEG-TS (*.ts);;FLV (*.flv);;MOV (*.mov);;All files (*)");
        if (!path.isEmpty()) m_outputPath->setText(path);
    }

    // ── Utilities ─────────────────────────────────────────────────────────────

    static QFrame* makeSeparator()
    {
        auto* f = new QFrame;
        f->setFrameShape(QFrame::HLine);
        f->setFrameShadow(QFrame::Sunken);
        return f;
    }

    static QString shellQuote(const QString& s)
    {
        return s.contains(' ') ? "\"" + s + "\"" : s;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Video Transcoding Pipeline");
    app.setStyle("Fusion");

    MainWindow w;
    w.show();

    return app.exec();
}

#include "gui.moc"
