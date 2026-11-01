package com.sunnyslopes.noslimefinder;

import javax.swing.*;
import javax.swing.filechooser.FileNameExtensionFilter;
import java.awt.*;
import java.awt.event.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.MessageFormat;
import java.util.*;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

/** Single-panel NoSlimeFinder GUI, laid out like the original single-seed tab. */
public class NoSlimeFinderFrame extends JFrame {

    private static final int DEFAULT_MIN_X = -65536;
    private static final int DEFAULT_MAX_X = 65535;
    private static final int DEFAULT_MIN_Z = -65536;
    private static final int DEFAULT_MAX_Z = 65535;
    private static final int BORDER_MIN = -30000000;
    private static final int BORDER_MAX = 30000000;
    private static final int DEFAULT_SIDE = DEFAULT_MAX_X - DEFAULT_MIN_X + 1;
    private static final int FIXED_RESULTS = NoSlimeFinderSearchRunner.DEFAULT_MAX_RESULTS;
    private static final int DEFAULT_Y = -63;
    private static final int DEFAULT_MIN_AREA = 49000;
    private static final int MAX_AREA = 49640;

    private ResourceBundle messages;
    private Locale locale;
    private Font loadedFont;

    private JLabel seedLabel, threadsLabel, versionLabel, biomeYLabel, avoidLabel,
        minAreaLabel, squareSideLabel, minXLabel, maxXLabel, minZLabel, maxZLabel,
        languageLabel, biomeSummaryLabel, elapsedLabel, remainingLabel,
        creditAuthorLabel, creditSlimeLabel, creditBiomesLabel;
    private JTextField seedField, threadsField, biomeYField, minAreaField,
        squareSideField, minXField, maxXField, minZField, maxZField;
    private JCheckBox squareSideCheck;
    private JComboBox<String> versionCombo, languageCombo;
    private JButton biomeButton, startButton, pauseButton, stopButton, resetButton,
        borderButton, exportButton, sortAreaButton, sortDistanceButton;
    private JProgressBar progressBar;
    private JTextArea resultArea;
    private JPanel resultPanel;

    private NoSlimeFinderSearchRunner runner;
    private final List<SlimeResult> currentResults = new ArrayList<>();
    private final Set<Integer> avoidBiomes = new TreeSet<>();
    private final Map<Integer, String> biomeEnglish = new LinkedHashMap<>();
    private final Map<String, Integer> biomeIdByName = new HashMap<>();
    private final Properties biomeZh = new Properties();
    private final Properties presets = new Properties();
    private final AtomicLong searchToken = new AtomicLong();

    private volatile boolean running = false;
    private volatile boolean paused = false;
    private volatile boolean stopRequested = false;
    private volatile boolean updatingBounds = false;
    private volatile boolean updatingVersion = false;
    private boolean nativeErrorShown = false;
    private boolean hasSavedAvoidSelection = false;
    private boolean initialBiomeDefaultsApplied = false;

    private Path configFile;

    public NoSlimeFinderFrame() {
        locale = defaultLocale();
        messages = ResourceBundle.getBundle("messages", locale);
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setLayout(new BorderLayout());
        setIconImage(loadIcon());
        loadFont();
        loadBiomeZh();
        loadPresets();
        buildUI();
        loadConfig();
        refreshBiomeList();
        applyDefaultAvoidSelection();
        applyTexts();
        pack();
        Dimension screen = Toolkit.getDefaultToolkit().getScreenSize();
        int height = Math.min(1000, Math.max(760, screen.height - 20));
        setSize(1500, height);
        setMinimumSize(new Dimension(1200, 700));
        setLocationRelativeTo(null);
        addWindowListener(new WindowAdapter() {
            @Override public void windowClosing(WindowEvent e) { saveConfig(); }
        });
    }

    private void applyDefaultAvoidSelection() {
        if (initialBiomeDefaultsApplied)
            return;
        initialBiomeDefaultsApplied = true;
        if (hasSavedAvoidSelection)
            return;
        avoidBiomes.clear();
        addDefaultBiome("mushroom_fields");
        addDefaultBiome("deep_dark");
        updateBiomeSummary();
    }

    private void addDefaultBiome(String english) {
        Integer id = biomeIdByName.get(english);
        if (id != null)
            avoidBiomes.add(id);
    }

    private static Locale defaultLocale() {
        Locale sys = Locale.getDefault();
        if ("zh".equals(sys.getLanguage())) return new Locale("zh", "CN");
        return new Locale("en", "US");
    }

    private Image loadIcon() {
        try (InputStream is = getClass().getResourceAsStream("/icon.png")) {
            if (is != null) return new ImageIcon(is.readAllBytes()).getImage();
        } catch (Exception ignored) {
        }
        return null;
    }

    private void loadFont() {
        try (InputStream is = getClass().getResourceAsStream("/font.ttf")) {
            if (is != null) loadedFont = Font.createFont(Font.TRUETYPE_FONT, is).deriveFont(13f);
        } catch (Exception ignored) {
        }
        if (loadedFont == null) loadedFont = new Font(Font.SANS_SERIF, Font.PLAIN, 13);
    }

    private void loadBiomeZh() {
        try (InputStream is = getClass().getResourceAsStream("/biome_zh.properties")) {
            if (is != null) biomeZh.load(new InputStreamReader(is, StandardCharsets.UTF_8));
        } catch (Exception ignored) {
        }
    }

    private void loadPresets() {
        try (InputStream is = getClass().getResourceAsStream("/biome_presets.properties")) {
            if (is != null) presets.load(new InputStreamReader(is, StandardCharsets.UTF_8));
        } catch (Exception ignored) {
        }
    }

    // ------------------------------------------------------------------ UI

    private void buildUI() {
        JPanel mainPanel = new JPanel(new BorderLayout());
        mainPanel.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));

        JPanel leftPanel = new JPanel(new BorderLayout());
        JPanel inputPanel = new JPanel(new GridBagLayout());
        GridBagConstraints gbc = new GridBagConstraints();
        gbc.insets = new Insets(3, 5, 3, 5);
        gbc.anchor = GridBagConstraints.WEST;

        int row = 0;
        seedField = new JTextField("", 20);
        seedLabel = addRow(inputPanel, gbc, row++, "label.seed", seedField);

        threadsField = new JTextField(String.valueOf(
            Math.max(1, Runtime.getRuntime().availableProcessors())), 20);
        threadsLabel = addRow(inputPanel, gbc, row++, "label.threads", threadsField);

        versionCombo = new JComboBox<>();
        for (GameVersions.Option o : GameVersions.GROUPS) versionCombo.addItem(o.label());
        versionCombo.setSelectedItem(GameVersions.DEFAULT_LABEL);
        versionCombo.setFont(loadedFont);
        versionCombo.addActionListener(e -> {
            if (!updatingVersion) refreshBiomeList();
        });
        versionLabel = addRow(inputPanel, gbc, row++, "label.version", versionCombo);

        biomeYField = new JTextField(String.valueOf(DEFAULT_Y), 20);
        biomeYLabel = addRow(inputPanel, gbc, row++, "label.biomeY", biomeYField);

        biomeButton = new JButton();
        biomeButton.addActionListener(e -> openBiomeDialog());
        biomeSummaryLabel = new JLabel();
        biomeSummaryLabel.setFont(loadedFont);
        JPanel biomeRow = new JPanel(new BorderLayout(6, 0));
        biomeRow.add(biomeButton, BorderLayout.WEST);
        biomeRow.add(biomeSummaryLabel, BorderLayout.CENTER);
        avoidLabel = addRow(inputPanel, gbc, row++, "label.avoid", biomeRow);

        minAreaField = new JTextField(String.valueOf(DEFAULT_MIN_AREA), 20);
        minAreaLabel = addRow(inputPanel, gbc, row++, "label.minArea", minAreaField);

        squareSideCheck = new JCheckBox();
        squareSideCheck.setFont(loadedFont);
        squareSideCheck.setSelected(true);
        squareSideCheck.addActionListener(e -> {
            applyBoundEnableState();
            applySquareSide();
        });
        squareSideField = new JTextField(String.valueOf(DEFAULT_SIDE), 20);
        squareSideField.getDocument().addDocumentListener(new SimpleDocumentListener(this::applySquareSide));
        squareSideLabel = new JLabel();
        squareSideLabel.setFont(loadedFont);
        JPanel squareLabelRow = new JPanel(new FlowLayout(FlowLayout.LEFT, 4, 0));
        squareLabelRow.add(squareSideCheck);
        squareLabelRow.add(squareSideLabel);

        gbc.gridx = 0;
        gbc.gridy = row;
        gbc.gridwidth = 1;
        gbc.fill = GridBagConstraints.NONE;
        gbc.weightx = 0;
        inputPanel.add(squareLabelRow, gbc);
        gbc.gridx = 1;
        gbc.fill = GridBagConstraints.HORIZONTAL;
        gbc.weightx = 1.0;
        inputPanel.add(squareSideField, gbc);
        row++;

        minXField = new JTextField(String.valueOf(DEFAULT_MIN_X), 20);
        minXLabel = addRow(inputPanel, gbc, row++, "label.minX", minXField);
        maxXField = new JTextField(String.valueOf(DEFAULT_MAX_X), 20);
        maxXLabel = addRow(inputPanel, gbc, row++, "label.maxX", maxXField);
        minZField = new JTextField(String.valueOf(DEFAULT_MIN_Z), 20);
        minZLabel = addRow(inputPanel, gbc, row++, "label.minZ", minZField);
        maxZField = new JTextField(String.valueOf(DEFAULT_MAX_Z), 20);
        maxZLabel = addRow(inputPanel, gbc, row++, "label.maxZ", maxZField);

        languageCombo = new JComboBox<>(new String[]{"中文", "English"});
        languageCombo.setFont(loadedFont);
        languageCombo.setSelectedIndex("zh".equals(locale.getLanguage()) ? 0 : 1);
        languageCombo.addActionListener(e -> changeLanguage());
        languageLabel = addRow(inputPanel, gbc, row++, "label.language", languageCombo);

        leftPanel.add(inputPanel, BorderLayout.NORTH);

        JPanel buttonPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 6, 4));
        startButton = new JButton();
        pauseButton = new JButton();
        stopButton = new JButton();
        resetButton = new JButton();
        borderButton = new JButton();
        startButton.addActionListener(e -> startSearch());
        pauseButton.addActionListener(e -> togglePause());
        stopButton.addActionListener(e -> stopSearch());
        resetButton.addActionListener(e -> resetDefaultRange());
        borderButton.addActionListener(e -> resetWorldBorder());
        pauseButton.setEnabled(false);
        stopButton.setEnabled(false);
        buttonPanel.add(startButton);
        buttonPanel.add(pauseButton);
        buttonPanel.add(stopButton);
        buttonPanel.add(resetButton);
        buttonPanel.add(borderButton);

        JPanel creditPanel = new JPanel();
        creditPanel.setLayout(new BoxLayout(creditPanel, BoxLayout.Y_AXIS));
        creditPanel.setBorder(BorderFactory.createEmptyBorder(8, 4, 4, 4));
        creditAuthorLabel = createCreditLabel();
        creditSlimeLabel = createCreditLabel();
        creditBiomesLabel = createCreditLabel();
        creditPanel.add(creditAuthorLabel);
        creditPanel.add(creditSlimeLabel);
        creditPanel.add(creditBiomesLabel);

        JPanel creditButtonPanel = new JPanel(new BorderLayout());
        creditButtonPanel.add(creditPanel, BorderLayout.NORTH);
        creditButtonPanel.add(buttonPanel, BorderLayout.SOUTH);
        leftPanel.add(creditButtonPanel, BorderLayout.CENTER);

        JPanel progressPanel = new JPanel(new GridBagLayout());
        GridBagConstraints pg = new GridBagConstraints();
        pg.insets = new Insets(4, 5, 4, 5);
        pg.anchor = GridBagConstraints.WEST;
        pg.fill = GridBagConstraints.HORIZONTAL;
        pg.weightx = 1.0;
        pg.gridx = 0;
        pg.gridy = 0;
        pg.gridwidth = 2;
        progressBar = new JProgressBar(0, 100);
        progressBar.setStringPainted(true);
        progressPanel.add(progressBar, pg);
        pg.gridwidth = 1;
        pg.gridy = 1;
        elapsedLabel = new JLabel();
        elapsedLabel.setFont(loadedFont);
        progressPanel.add(elapsedLabel, pg);
        pg.gridy = 2;
        remainingLabel = new JLabel();
        remainingLabel.setFont(loadedFont);
        progressPanel.add(remainingLabel, pg);

        leftPanel.add(progressPanel, BorderLayout.SOUTH);

        JPanel leftContainer = new JPanel(new BorderLayout());
        leftContainer.add(leftPanel, BorderLayout.CENTER);

        resultPanel = new JPanel(new BorderLayout(4, 4));
        resultPanel.setBorder(BorderFactory.createTitledBorder(""));
        resultArea = new JTextArea();
        resultArea.setEditable(false);
        resultArea.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
        JScrollPane scroll = new JScrollPane(resultArea);
        scroll.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_ALWAYS);
        resultPanel.add(scroll, BorderLayout.CENTER);

        JPanel resultButtons = new JPanel(new FlowLayout(FlowLayout.CENTER, 6, 4));
        exportButton = new JButton();
        sortAreaButton = new JButton();
        sortDistanceButton = new JButton();
        exportButton.addActionListener(e -> exportResults());
        sortAreaButton.addActionListener(e -> sortResults(true));
        sortDistanceButton.addActionListener(e -> sortResults(false));
        resultButtons.add(exportButton);
        resultButtons.add(sortAreaButton);
        resultButtons.add(sortDistanceButton);
        resultPanel.add(resultButtons, BorderLayout.SOUTH);

        JSplitPane splitPane = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,
            leftContainer, resultPanel);
        splitPane.setDividerLocation(600);
        splitPane.setResizeWeight(0.5);
        mainPanel.add(splitPane, BorderLayout.CENTER);
        setContentPane(mainPanel);

        applyBoundEnableState();
        applySquareSide();
        applyFont(mainPanel);
        addFieldValidation();
    }

    private JLabel createCreditLabel() {
        JLabel label = new JLabel();
        label.setFont(loadedFont);
        label.setAlignmentX(Component.CENTER_ALIGNMENT);
        label.setHorizontalAlignment(SwingConstants.CENTER);
        return label;
    }

    private JLabel addRow(JPanel panel, GridBagConstraints gbc, int row,
                          String key, JComponent field) {
        JLabel label = new JLabel();
        label.setFont(loadedFont);
        gbc.gridx = 0;
        gbc.gridy = row;
        gbc.gridwidth = 1;
        gbc.fill = GridBagConstraints.NONE;
        gbc.weightx = 0;
        panel.add(label, gbc);
        gbc.gridx = 1;
        gbc.fill = GridBagConstraints.HORIZONTAL;
        gbc.weightx = 1.0;
        panel.add(field, gbc);
        return label;
    }

    private void applyFont(Component c) {
        if (c instanceof JLabel l) l.setFont(loadedFont);
        else if (c instanceof JButton b) b.setFont(loadedFont);
        else if (c instanceof JCheckBox b) b.setFont(loadedFont);
        else if (c instanceof JComboBox<?> b) b.setFont(loadedFont);
        else if (c instanceof JTextField b) b.setFont(loadedFont);
        else if (c instanceof JProgressBar b) b.setFont(loadedFont);
        if (c instanceof Container container) {
            for (Component child : container.getComponents()) applyFont(child);
        }
    }

    private void addFieldValidation() {
        FocusAdapter fa = new FocusAdapter() {
            @Override public void focusLost(FocusEvent e) {
                if (e.getSource() instanceof JTextField tf) {
                    String t = tf.getText().trim();
                    if (t.isEmpty()) return;
                    try {
                        Long.parseLong(t);
                    } catch (NumberFormatException ex) {
                        JOptionPane.showMessageDialog(NoSlimeFinderFrame.this,
                            getString("error.range"), getString("window.title"),
                            JOptionPane.ERROR_MESSAGE);
                    }
                }
            }
        };
        for (JTextField f : new JTextField[]{seedField, threadsField, biomeYField,
            minAreaField, squareSideField, minXField, maxXField, minZField, maxZField}) {
            f.addFocusListener(fa);
        }
    }

    // ---------------------------------------------------------------- biomes

    private String selectedMc() {
        return GameVersions.mcForLabel((String) versionCombo.getSelectedItem());
    }

    private void refreshBiomeList() {
        String mc = selectedMc();
        biomeEnglish.clear();
        biomeIdByName.clear();
        try {
            String[] entries = NoSlimeFinderBridge.listBiomes(mc);
            if (entries != null) {
                for (String entry : entries) {
                    int p = entry.indexOf('|');
                    if (p <= 0) continue;
                    try {
                        int id = Integer.parseInt(entry.substring(0, p));
                        String name = entry.substring(p + 1);
                        biomeEnglish.put(id, name);
                        biomeIdByName.put(name, id);
                        String zh = biomeZh.getProperty(name);
                        if (zh != null && !zh.isBlank()) biomeIdByName.putIfAbsent(zh, id);
                    } catch (NumberFormatException ignored) {
                    }
                }
            }
        } catch (Throwable t) {
            if (!nativeErrorShown) {
                nativeErrorShown = true;
                JOptionPane.showMessageDialog(this,
                    getString("error.native") + "\n" + t.getMessage(),
                    getString("window.title"), JOptionPane.ERROR_MESSAGE);
            }
        }
        avoidBiomes.removeIf(id -> !biomeEnglish.containsKey(id));
        updateBiomeSummary();
    }

    private String displayBiome(int id) {
        String en = biomeEnglish.getOrDefault(id, String.valueOf(id));
        if ("zh".equals(locale.getLanguage())) {
            String zh = biomeZh.getProperty(en);
            if (zh != null && !zh.isBlank()) return zh;
        }
        return en;
    }

    private void updateBiomeSummary() {
        if (biomeSummaryLabel == null) return;
        if (avoidBiomes.isEmpty()) {
            biomeSummaryLabel.setText(getString("summary.none"));
            return;
        }
        StringBuilder sb = new StringBuilder();
        int i = 0;
        for (int id : avoidBiomes) {
            if (i >= 6) {
                sb.append(getString("summary.more"));
                break;
            }
            if (i > 0) sb.append(", ");
            sb.append(displayBiome(id));
            i++;
        }
        biomeSummaryLabel.setText(getString("summary.selected", avoidBiomes.size(), sb));
        biomeSummaryLabel.setToolTipText(allBiomeNames());
    }

    private String allBiomeNames() {
        StringBuilder sb = new StringBuilder("<html>");
        for (int id : avoidBiomes) sb.append(displayBiome(id)).append("<br>");
        return sb.append("</html>").toString();
    }

    private static int biomeGroupIndex(String english) {
        if ("mushroom_fields".equals(english) || "deep_dark".equals(english)) {
            return 0;
        }
        if ("river".equals(english) || "frozen_river".equals(english)) {
            return 1;
        }
        if ("dripstone_caves".equals(english)
            || "lush_caves".equals(english)
            || "sulfur_caves".equals(english)) {
            return 2;
        }
        if (english.contains("ocean")) {
            return 3;
        }
        return 4;
    }

    private void openBiomeDialog() {
        if (biomeEnglish.isEmpty()) {
            JOptionPane.showMessageDialog(this, getString("error.biomes", "-"),
                getString("window.title"), JOptionPane.WARNING_MESSAGE);
            return;
        }
        JDialog dialog = new JDialog(this, getString("dialog.biomes"), true);
        dialog.setLayout(new BorderLayout(6, 6));
        dialog.setSize(720, 680);
        dialog.setLocationRelativeTo(this);

        JTextField filter = new JTextField();
        filter.setFont(loadedFont);
        JPanel top = new JPanel(new BorderLayout(4, 4));
        top.setBorder(BorderFactory.createEmptyBorder(6, 6, 0, 6));
        top.add(filter, BorderLayout.CENTER);
        dialog.add(top, BorderLayout.NORTH);

        JPanel listPanel = new JPanel();
        listPanel.setLayout(new BoxLayout(listPanel, BoxLayout.Y_AXIS));
        Map<Integer, JCheckBox> boxes = new LinkedHashMap<>();

        String[] groupKeys = {
            "group.required", "group.rivers", "group.caves",
            "group.ocean", "group.default"
        };
        List<JPanel> groupPanels = new ArrayList<>();
        List<List<JCheckBox>> groupCheckboxes = new ArrayList<>();
        for (String groupKey : groupKeys) {
            final List<JCheckBox> groupList = new ArrayList<>();
            JPanel itemsPanel = new JPanel();
            itemsPanel.setLayout(new BoxLayout(itemsPanel, BoxLayout.Y_AXIS));
            itemsPanel.setBorder(BorderFactory.createEmptyBorder(0, 14, 0, 0));
            itemsPanel.setAlignmentX(Component.LEFT_ALIGNMENT);

            JPanel header = new JPanel(new BorderLayout(4, 0));
            JLabel title = new JLabel(getString(groupKey));
            title.setFont(loadedFont.deriveFont(Font.BOLD));
            header.add(title, BorderLayout.CENTER);

            JPanel groupButtons = new JPanel(new FlowLayout(FlowLayout.RIGHT, 4, 0));
            JButton groupAll = new JButton(getString("button.groupAll"));
            JButton groupNone = new JButton(getString("button.groupNone"));
            groupAll.setFont(loadedFont.deriveFont(11f));
            groupNone.setFont(loadedFont.deriveFont(11f));
            groupAll.setMargin(new Insets(0, 4, 0, 4));
            groupNone.setMargin(new Insets(0, 4, 0, 4));
            groupAll.addActionListener(e -> {
                for (JCheckBox cb : groupList) cb.setSelected(true);
            });
            groupNone.addActionListener(e -> {
                for (JCheckBox cb : groupList) cb.setSelected(false);
            });
            groupButtons.add(groupAll);
            groupButtons.add(groupNone);
            header.add(groupButtons, BorderLayout.EAST);

            JPanel groupPanel = new JPanel(new BorderLayout());
            groupPanel.setAlignmentX(Component.LEFT_ALIGNMENT);
            groupPanel.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createMatteBorder(1, 0, 0, 0, new Color(220, 220, 220)),
                BorderFactory.createEmptyBorder(4, 4, 4, 4)));
            groupPanel.add(header, BorderLayout.NORTH);
            groupPanel.add(itemsPanel, BorderLayout.CENTER);
            listPanel.add(groupPanel);
            groupPanels.add(groupPanel);
            groupCheckboxes.add(groupList);
        }

        for (Map.Entry<Integer, String> e : biomeEnglish.entrySet()) {
            JCheckBox cb = new JCheckBox(displayNameWithEnglish(e.getKey()));
            cb.setFont(loadedFont);
            cb.setAlignmentX(Component.LEFT_ALIGNMENT);
            cb.setSelected(avoidBiomes.contains(e.getKey()));
            cb.putClientProperty("english", e.getValue());
            boxes.put(e.getKey(), cb);
            int group = biomeGroupIndex(e.getValue());
            JPanel itemsPanel = (JPanel) ((JPanel) groupPanels.get(group).getComponent(1));
            itemsPanel.add(cb);
            groupCheckboxes.get(group).add(cb);
        }
        JScrollPane scroll = new JScrollPane(listPanel);
        dialog.add(scroll, BorderLayout.CENTER);

        Runnable filterAction = () -> {
            String q = filter.getText().trim().toLowerCase(Locale.ROOT);
            for (int g = 0; g < groupPanels.size(); g++) {
                boolean anyVisible = false;
                for (JCheckBox cb : groupCheckboxes.get(g)) {
                    String en = String.valueOf(cb.getClientProperty("english"))
                        .toLowerCase(Locale.ROOT);
                    String display = cb.getText().toLowerCase(Locale.ROOT);
                    boolean visible = q.isEmpty() || en.contains(q) || display.contains(q);
                    cb.setVisible(visible);
                    anyVisible |= visible;
                }
                groupPanels.get(g).setVisible(anyVisible);
            }
            listPanel.revalidate();
            listPanel.repaint();
        };
        filter.getDocument().addDocumentListener(new SimpleDocumentListener(filterAction::run));

        JPanel presetsPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 4, 2));
        buildPresetButtons(presetsPanel, boxes);
        JPanel south = new JPanel(new FlowLayout(FlowLayout.RIGHT, 6, 6));
        JButton all = new JButton(getString("button.selectAll"));
        JButton none = new JButton(getString("button.clear"));
        JButton importBtn = new JButton(getString("button.import"));
        JButton exportBtn = new JButton(getString("button.exportList"));
        JButton ok = new JButton(getString("button.ok"));
        JButton cancel = new JButton(getString("button.cancel"));
        all.addActionListener(e -> {
            for (JCheckBox cb : boxes.values()) if (cb.isVisible()) cb.setSelected(true);
        });
        none.addActionListener(e -> {
            for (JCheckBox cb : boxes.values()) cb.setSelected(false);
        });
        importBtn.addActionListener(e -> importBiomes(dialog, boxes, listPanel));
        exportBtn.addActionListener(e -> exportBiomes(boxes));
        ok.addActionListener(e -> {
            avoidBiomes.clear();
            for (Map.Entry<Integer, JCheckBox> entry : boxes.entrySet()) {
                if (entry.getValue().isSelected()) avoidBiomes.add(entry.getKey());
            }
            updateBiomeSummary();
            saveConfig();
            dialog.dispose();
        });
        cancel.addActionListener(e -> dialog.dispose());
        south.add(all);
        south.add(none);
        south.add(importBtn);
        south.add(exportBtn);
        south.add(ok);
        south.add(cancel);

        JPanel bottom = new JPanel(new BorderLayout());
        bottom.add(presetsPanel, BorderLayout.NORTH);
        bottom.add(south, BorderLayout.SOUTH);
        dialog.add(bottom, BorderLayout.SOUTH);
        dialog.setVisible(true);
    }

    private String displayNameWithEnglish(int id) {
        String en = biomeEnglish.get(id);
        if ("zh".equals(locale.getLanguage())) {
            String zh = biomeZh.getProperty(en);
            if (zh != null && !zh.isBlank() && !zh.equals(en)) return zh + " (" + en + ")";
        }
        return en;
    }

    private void buildPresetButtons(JPanel panel, Map<Integer, JCheckBox> boxes) {
        for (String key : presets.stringPropertyNames()) {
            if (!key.startsWith("preset.") || !key.endsWith(".label")) continue;
            String base = key.substring(0, key.length() - ".label".length());
            String label = presets.getProperty(key);
            String list = presets.getProperty(base + ".biomes");
            if (list == null) continue;
            JButton b = new JButton(label);
            b.setFont(loadedFont);
            b.addActionListener(e -> {
                for (String tok : list.split(",")) {
                    Integer id = resolveBiomeToken(tok.trim());
                    JCheckBox cb = id == null ? null : boxes.get(id);
                    if (cb != null) cb.setSelected(true);
                }
            });
            panel.add(b);
        }
    }

    private Integer resolveBiomeToken(String token) {
        if (token == null || token.isBlank()) return null;
        try {
            int id = Integer.parseInt(token);
            return biomeEnglish.containsKey(id) ? id : null;
        } catch (NumberFormatException ignored) {
        }
        return biomeIdByName.get(token);
    }

    private void importBiomes(JDialog dialog, Map<Integer, JCheckBox> boxes, JPanel listPanel) {
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle(getString("dialog.import"));
        chooser.setFileFilter(new FileNameExtensionFilter(getString("dialog.textFiles"), "txt"));
        if (chooser.showOpenDialog(dialog) != JFileChooser.APPROVE_OPTION) return;
        try {
            List<String> lines = Files.readAllLines(chooser.getSelectedFile().toPath(), StandardCharsets.UTF_8);
            int added = 0;
            for (String line : lines) {
                String t = line.trim();
                if (t.isEmpty() || t.startsWith("#")) continue;
                Integer id = resolveBiomeToken(t);
                if (id != null) {
                    JCheckBox cb = boxes.get(id);
                    if (cb != null) {
                        cb.setSelected(true);
                        added++;
                    }
                }
            }
            JOptionPane.showMessageDialog(dialog, getString("success.import", added),
                getString("dialog.import"), JOptionPane.INFORMATION_MESSAGE);
            listPanel.revalidate();
            listPanel.repaint();
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(dialog, getString("error.import", ex.getMessage()),
                getString("prompt.error"), JOptionPane.ERROR_MESSAGE);
        }
    }

    private void exportBiomes(Map<Integer, JCheckBox> boxes) {
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle(getString("dialog.export"));
        chooser.setFileFilter(new FileNameExtensionFilter(getString("dialog.textFiles"), "txt"));
        chooser.setSelectedFile(new File("biomes.txt"));
        if (chooser.showSaveDialog(this) != JFileChooser.APPROVE_OPTION) return;
        try (BufferedWriter w = Files.newBufferedWriter(chooser.getSelectedFile().toPath(),
                StandardCharsets.UTF_8)) {
            for (Map.Entry<Integer, JCheckBox> e : boxes.entrySet()) {
                if (e.getValue().isSelected()) {
                    w.write(biomeEnglish.getOrDefault(e.getKey(), String.valueOf(e.getKey())));
                    w.newLine();
                }
            }
            JOptionPane.showMessageDialog(this, getString("success.export"),
                getString("window.title"), JOptionPane.INFORMATION_MESSAGE);
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, getString("error.export", ex.getMessage()),
                getString("prompt.error"), JOptionPane.ERROR_MESSAGE);
        }
    }

    // ----------------------------------------------------------------- search

    private void startSearch() {
        if (running && paused) {
            togglePause();
            return;
        }
        if (NoSlimeFinderSearchRunner.isAnySearchActive()) {
            JOptionPane.showMessageDialog(this, getString("error.busy"),
                getString("window.title"), JOptionPane.WARNING_MESSAGE);
            return;
        }
        final long seed;
        final int threads;
        final int biomeY;
        final int minArea;
        final int minX, maxX, minZ, maxZ;
        try {
            seed = Long.parseLong(seedField.getText().trim());
        } catch (NumberFormatException e) {
            JOptionPane.showMessageDialog(this, getString("error.seed"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            seedField.requestFocus();
            return;
        }
        try {
            threads = Integer.parseInt(threadsField.getText().trim());
        } catch (NumberFormatException e) {
            JOptionPane.showMessageDialog(this, getString("error.threads"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (threads < 1 || threads > 256) {
            JOptionPane.showMessageDialog(this, getString("error.threads"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        int cpu = Runtime.getRuntime().availableProcessors();
        if (threads > cpu) {
            int r = JOptionPane.showConfirmDialog(this, getString("error.threadsHigh", cpu),
                getString("window.title"), JOptionPane.YES_NO_OPTION, JOptionPane.QUESTION_MESSAGE);
            if (r == JOptionPane.YES_OPTION) {
                threadsField.setText(String.valueOf(cpu));
                return;
            }
        }
        try {
            biomeY = Integer.parseInt(biomeYField.getText().trim());
        } catch (NumberFormatException e) {
            JOptionPane.showMessageDialog(this, getString("error.y"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (biomeY < -64 || biomeY > 319) {
            JOptionPane.showMessageDialog(this, getString("error.y"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        try {
            minArea = Integer.parseInt(minAreaField.getText().trim());
        } catch (NumberFormatException e) {
            JOptionPane.showMessageDialog(this, getString("error.minArea"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (minArea < 0 || minArea > MAX_AREA) {
            JOptionPane.showMessageDialog(this, getString("error.minArea"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        try {
            minX = Integer.parseInt(minXField.getText().trim());
            maxX = Integer.parseInt(maxXField.getText().trim());
            minZ = Integer.parseInt(minZField.getText().trim());
            maxZ = Integer.parseInt(maxZField.getText().trim());
        } catch (NumberFormatException e) {
            JOptionPane.showMessageDialog(this, getString("error.range"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (minX > maxX || minZ > maxZ) {
            JOptionPane.showMessageDialog(this, getString("error.range"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        if (minX < BORDER_MIN || maxX > BORDER_MAX || minZ < BORDER_MIN || maxZ > BORDER_MAX) {
            JOptionPane.showMessageDialog(this, getString("error.rangeOut"),
                getString("window.title"), JOptionPane.ERROR_MESSAGE);
            return;
        }
        long width = (long) maxX - minX + 1;
        long height = (long) maxZ - minZ + 1;
        long maxSide = (long) width * height;
        if (maxSide > 10_000_001L*10_000_001L) {
            int r = JOptionPane.showConfirmDialog(this, getString("error.rangeBig", maxSide),
                getString("window.title"), JOptionPane.YES_NO_OPTION, JOptionPane.WARNING_MESSAGE);
            if (r != JOptionPane.YES_OPTION) return;
        }

        currentResults.clear();
        renderResults();
        progressBar.setValue(0);
        progressBar.setString(getString("progress.waiting"));
        elapsedLabel.setText(getString("progress.elapsed", formatTime(0)));
        remainingLabel.setText(getString("progress.remaining.calculating"));
        running = true;
        paused = false;
        stopRequested = false;
        setControlsEnabled(false);
        pauseButton.setEnabled(true);
        stopButton.setEnabled(true);
        pauseButton.setText(getString("button.pause"));
        final long token = searchToken.incrementAndGet();
        runner = new NoSlimeFinderSearchRunner();
        int[] avoid = avoidBiomes.stream().mapToInt(Integer::intValue).toArray();
        String mc = selectedMc();
        saveConfig();

        boolean started = runner.startSearch(seed, minX, maxX, minZ, maxZ, threads,
            mc, biomeY, avoid, FIXED_RESULTS, minArea,
            info -> SwingUtilities.invokeLater(() -> {
                if (token != searchToken.get()) return;
                updateProgress(info);
            }),
            results -> SwingUtilities.invokeLater(() -> {
                if (token != searchToken.get() || stopRequested) return;
                currentResults.clear();
                currentResults.addAll(results);
                currentResults.sort(areaComparator());
                renderResults();
                exportButton.setEnabled(!currentResults.isEmpty());
                sortAreaButton.setEnabled(!currentResults.isEmpty());
                sortDistanceButton.setEnabled(!currentResults.isEmpty());
            }),
            error -> SwingUtilities.invokeLater(() -> {
                if (token != searchToken.get()) return;
                finishSearch();
                JOptionPane.showMessageDialog(this, getString("error.search", error),
                    getString("window.title"), JOptionPane.ERROR_MESSAGE);
            }));
        if (!started) {
            finishSearch();
            JOptionPane.showMessageDialog(this, getString("error.busy"),
                getString("window.title"), JOptionPane.WARNING_MESSAGE);
        }
    }

    private void updateProgress(NoSlimeFinderSearchRunner.ProgressInfo info) {
        int stage = info.stage();
        String stageName = switch (stage) {
            case 1 -> getString("progress.stage1");
            case 2 -> getString("progress.stage2");
            case 3 -> getString("progress.stage3");
            case 4 -> getString("progress.stage4");
            case 6 -> getString("progress.stage6");
            case 7 -> getString("progress.stage7");
            default -> getString("progress.stage5");
        };
        int pct = (int) Math.round(Math.max(0, Math.min(100, info.percent())));
        progressBar.setValue(pct);
        progressBar.setString(stageName + "  " + getString("progress.format",
            info.current(), info.total(), info.percent()));
        elapsedLabel.setText(getString("progress.elapsed", formatTime(info.elapsedMs())));
        if (info.done()) {
            remainingLabel.setText(getString("progress.remaining.done"));
        } else if (info.paused()) {
            remainingLabel.setText(getString("progress.remaining.paused"));
        } else if (info.remainingMs() > 0) {
            remainingLabel.setText(getString("progress.remaining", formatTime(info.remainingMs())));
        } else {
            remainingLabel.setText(getString("progress.remaining.calculating"));
        }
        paused = info.paused();
        pauseButton.setText(paused ? getString("button.resume") : getString("button.pause"));
        if (info.done()) finishSearch();
    }

    private void finishSearch() {
        if (!running) return;
        running = false;
        paused = false;
        setControlsEnabled(true);
        pauseButton.setEnabled(false);
        stopButton.setEnabled(false);
        pauseButton.setText(getString("button.pause"));
        if (progressBar.getValue() >= 100) {
            progressBar.setString(getString("progress.remaining.done"));
        }
    }

    private void setControlsEnabled(boolean enabled) {
        seedField.setEnabled(enabled);
        threadsField.setEnabled(enabled);
        versionCombo.setEnabled(enabled);
        biomeYField.setEnabled(enabled);
        minAreaField.setEnabled(enabled);
        biomeButton.setEnabled(enabled);
        squareSideCheck.setEnabled(enabled);
        squareSideField.setEnabled(enabled && squareSideCheck.isSelected());
        minXField.setEnabled(enabled && !squareSideCheck.isSelected());
        maxXField.setEnabled(enabled && !squareSideCheck.isSelected());
        minZField.setEnabled(enabled && !squareSideCheck.isSelected());
        maxZField.setEnabled(enabled && !squareSideCheck.isSelected());
        resetButton.setEnabled(enabled);
        borderButton.setEnabled(enabled);
        startButton.setEnabled(enabled);
        languageCombo.setEnabled(enabled);
    }

    private void togglePause() {
        if (!running || runner == null) return;
        if (paused) {
            runner.resume();
            paused = false;
            pauseButton.setText(getString("button.pause"));
        } else {
            runner.pause();
            paused = true;
            pauseButton.setText(getString("button.resume"));
        }
    }

    private void stopSearch() {
        if (!running || runner == null) return;
        stopRequested = true;
        runner.stop();
        currentResults.clear();
        renderResults();
        exportButton.setEnabled(false);
        sortAreaButton.setEnabled(false);
        sortDistanceButton.setEnabled(false);
        progressBar.setString(getString("progress.stage6"));
        remainingLabel.setText(getString("progress.remaining.paused"));
    }

    private void sortResults(boolean byArea) {
        if (currentResults.isEmpty()) return;
        currentResults.sort(byArea ? areaComparator() : distanceComparator());
        renderResults();
    }

    private Comparator<SlimeResult> areaComparator() {
        return Comparator.comparingInt((SlimeResult r) -> r.area).reversed()
            .thenComparingLong(SlimeResult::distanceSq)
            .thenComparingInt(r -> r.x)
            .thenComparingInt(r -> r.z);
    }

    private Comparator<SlimeResult> distanceComparator() {
        return Comparator.comparingLong(SlimeResult::distanceSq)
            .thenComparing(Comparator.comparingInt((SlimeResult r) -> r.area).reversed())
            .thenComparingInt(r -> r.x)
            .thenComparingInt(r -> r.z);
    }

    private void renderResults() {
        StringBuilder sb = new StringBuilder();
        for (SlimeResult r : currentResults) sb.append(r.toLine()).append('\n');
        resultArea.setText(sb.toString());
        resultArea.setCaretPosition(0);
    }

    private void exportResults() {
        if (resultArea.getText().trim().isEmpty()) {
            JOptionPane.showMessageDialog(this, getString("error.noResults"),
                getString("window.title"), JOptionPane.INFORMATION_MESSAGE);
            return;
        }
        JFileChooser chooser = new JFileChooser();
        chooser.setDialogTitle(getString("dialog.exportResults"));
        chooser.setFileFilter(new FileNameExtensionFilter(getString("dialog.textFiles"), "txt"));
        chooser.setSelectedFile(new File("noslimefinder_results.txt"));
        if (chooser.showSaveDialog(this) != JFileChooser.APPROVE_OPTION) return;
        try {
            Files.writeString(chooser.getSelectedFile().toPath(), resultArea.getText(),
                StandardCharsets.UTF_8);
            JOptionPane.showMessageDialog(this, getString("success.export"),
                getString("window.title"), JOptionPane.INFORMATION_MESSAGE);
        } catch (Exception ex) {
            JOptionPane.showMessageDialog(this, getString("error.export", ex.getMessage()),
                getString("prompt.error"), JOptionPane.ERROR_MESSAGE);
        }
    }

    // ------------------------------------------------------------------- range

    private void applySquareSide() {
        if (updatingBounds || !squareSideCheck.isSelected()) return;
        try {
            int side = Integer.parseInt(squareSideField.getText().trim());
            if (side <= 0) return;
            int min = -side / 2;
            int max = min + side - 1;
            updatingBounds = true;
            minXField.setText(String.valueOf(min));
            maxXField.setText(String.valueOf(max));
            minZField.setText(String.valueOf(min));
            maxZField.setText(String.valueOf(max));
        } catch (NumberFormatException ignored) {
        } finally {
            updatingBounds = false;
        }
    }

    private void applyBoundEnableState() {
        boolean square = squareSideCheck.isSelected();
        squareSideField.setEnabled(!running && square);
        minXField.setEnabled(!running && !square);
        maxXField.setEnabled(!running && !square);
        minZField.setEnabled(!running && !square);
        maxZField.setEnabled(!running && !square);
    }

    private void resetDefaultRange() {
        squareSideCheck.setSelected(true);
        squareSideField.setText(String.valueOf(DEFAULT_SIDE));
        applyBoundEnableState();
        applySquareSide();
        biomeYField.setText(String.valueOf(DEFAULT_Y));
    }

    private void resetWorldBorder() {
        squareSideCheck.setSelected(false);
        updatingBounds = true;
        minXField.setText(String.valueOf(BORDER_MIN));
        maxXField.setText(String.valueOf(BORDER_MAX));
        minZField.setText(String.valueOf(BORDER_MIN));
        maxZField.setText(String.valueOf(BORDER_MAX));
        updatingBounds = false;
        applyBoundEnableState();
    }

    // ----------------------------------------------------------------- config

    private void loadConfig() {
        Path dir = Paths.get(System.getProperty("user.home"), ".noslimefinder");
        configFile = dir.resolve("config.properties");
        if (!Files.exists(configFile)) return;
        Properties p = new Properties();
        try (Reader r = Files.newBufferedReader(configFile, StandardCharsets.UTF_8)) {
            p.load(r);
        } catch (Exception ignored) {
            return;
        }
        try {
            if (p.getProperty("threads") != null) threadsField.setText(p.getProperty("threads"));
            if (p.getProperty("biomeY") != null) biomeYField.setText(p.getProperty("biomeY"));
            if (p.getProperty("minArea") != null) minAreaField.setText(p.getProperty("minArea"));
            String mc = p.getProperty("version");
            if (mc != null) selectVersionMc(mc);
            hasSavedAvoidSelection = p.getProperty("avoidBiomes") != null;
            String avoid = p.getProperty("avoidBiomes", "");
            for (String s : avoid.split(",")) {
                if (s.isBlank()) continue;
                try {
                    avoidBiomes.add(Integer.parseInt(s.trim()));
                } catch (NumberFormatException ignored) {
                }
            }
            refreshBiomeList();
        } catch (Exception ignored) {
        }
    }

    private void selectVersionMc(String mc) {
        for (int i = 0; i < versionCombo.getItemCount(); i++) {
            String label = versionCombo.getItemAt(i);
            if (GameVersions.mcForLabel(label).equals(mc)) {
                updatingVersion = true;
                versionCombo.setSelectedIndex(i);
                updatingVersion = false;
                return;
            }
        }
    }

    private void saveConfig() {
        if (configFile == null) return;
        Properties p = new Properties();
        p.setProperty("threads", threadsField.getText().trim());
        p.setProperty("biomeY", biomeYField.getText().trim());
        p.setProperty("minArea", minAreaField.getText().trim());
        p.setProperty("version", selectedMc());
        StringBuilder sb = new StringBuilder();
        for (int id : avoidBiomes) {
            if (sb.length() > 0) sb.append(',');
            sb.append(id);
        }
        p.setProperty("avoidBiomes", sb.toString());
        try {
            Files.createDirectories(configFile.getParent());
            try (Writer w = Files.newBufferedWriter(configFile, StandardCharsets.UTF_8)) {
                p.store(w, "NoSlimeFinder configuration");
            }
        } catch (Exception ignored) {
        }
    }

    // ------------------------------------------------------------------ i18n

    private void changeLanguage() {
        boolean zh = languageCombo.getSelectedIndex() == 0;
        Locale next = zh ? new Locale("zh", "CN") : new Locale("en", "US");
        if (next.getLanguage().equals(locale.getLanguage())) return;
        locale = next;
        messages = ResourceBundle.getBundle("messages", locale);
        applyTexts();
        refreshBiomeList();
        updateBiomeSummary();
    }

    private void applyTexts() {
        setTitle(getString("window.title"));
        seedLabel.setText(getString("label.seed"));
        threadsLabel.setText(getString("label.threads"));
        versionLabel.setText(getString("label.version"));
        biomeYLabel.setText(getString("label.biomeY"));
        avoidLabel.setText(getString("label.avoid"));
        biomeButton.setText(getString("button.selectBiomes"));
        minAreaLabel.setText(getString("label.minArea"));
        squareSideLabel.setText(getString("label.squareSide"));
        squareSideCheck.setText("");
        minXLabel.setText(getString("label.minX"));
        maxXLabel.setText(getString("label.maxX"));
        minZLabel.setText(getString("label.minZ"));
        maxZLabel.setText(getString("label.maxZ"));
        languageLabel.setText(getString("label.language"));
        startButton.setText(getString("button.startSearch"));
        pauseButton.setText(paused ? getString("button.resume") : getString("button.pause"));
        stopButton.setText(getString("button.stop"));
        resetButton.setText(getString("button.reset"));
        borderButton.setText(getString("button.resetBorder"));
        exportButton.setText(getString("button.export"));
        sortAreaButton.setText(getString("button.sortByArea"));
        sortDistanceButton.setText(getString("button.sortByDistance"));
        resultPanel.setBorder(BorderFactory.createTitledBorder(getString("result.border")));
        creditAuthorLabel.setText(getString("credit.author"));
        creditSlimeLabel.setText(getString("credit.slime"));
        creditBiomesLabel.setText(getString("credit.biomes"));
        if (!running) {
            progressBar.setString(getString("progress.waiting"));
            elapsedLabel.setText(getString("progress.elapsed", formatTime(0)));
            remainingLabel.setText(getString("progress.remaining.calculating"));
        }
        updateBiomeSummary();
        applyFont(getContentPane());
        resultArea.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
    }

    private String getString(String key) {
        try {
            return messages.getString(key);
        } catch (MissingResourceException e) {
            return key;
        }
    }

    private String getString(String key, Object... args) {
        try {
            return MessageFormat.format(messages.getString(key), args);
        } catch (MissingResourceException e) {
            return key;
        }
    }

    private static String formatTime(long ms) {
        long total = Math.max(0, ms) / 1000;
        long h = total / 3600;
        long m = (total % 3600) / 60;
        long s = total % 60;
        if (h > 0) return String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s);
        return String.format(Locale.ROOT, "%02d:%02d", m, s);
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> {
            try {
                UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
            } catch (Exception ignored) {
            }
            new NoSlimeFinderFrame().setVisible(true);
        });
    }

    private static final class SimpleDocumentListener implements javax.swing.event.DocumentListener {
        private final Runnable action;
        SimpleDocumentListener(Runnable action) { this.action = action; }
        @Override public void insertUpdate(javax.swing.event.DocumentEvent e) { action.run(); }
        @Override public void removeUpdate(javax.swing.event.DocumentEvent e) { action.run(); }
        @Override public void changedUpdate(javax.swing.event.DocumentEvent e) { action.run(); }
    }
}
