# Translations

Mirab's interface is translated with Qt Linguist. This folder holds one source file per language:

| File | Language | Direction |
|---|---|---|
| `mirab_fa.ts` | Persian (فارسی) | right to left |
| `mirab_ar.ts` | Arabic (العربية) | right to left |
| `mirab_he.ts` | Hebrew (עברית) | right to left |
| `mirab_es.ts` | Spanish (Español) | left to right |
| `mirab_pt_BR.ts` | Portuguese, Brazil (Português do Brasil) | left to right |
| `mirab_fr.ts` | French (Français) | left to right |
| `mirab_it.ts` | Italian (Italiano) | left to right |
| `mirab_de.ts` | German (Deutsch) | left to right |
| `mirab_zh_CN.ts` | Simplified Chinese (简体中文) | left to right |
| `mirab_zh_TW.ts` | Traditional Chinese (繁體中文) | left to right |

The build compiles each file into a `.qm` file and puts it inside the program, together with Qt's own
translations of the same languages (`qtbase_<lang>.qm`, for file dialogs and standard buttons). Packages
therefore need no extra files. The language is chosen in *Tools > Settings > Language* and takes effect
the next time Mirab starts; `mirab --language fa` starts in a given language once.

## What is translated

- Everything the interface says: menus, toolbars, dialogs, editors, reports, report documents, print
  layouts, messages.
- Texts that come from the Qt-free libraries (`src/core`, `src/engine`) and are shown in the interface:
  section descriptions, column help, element kinds, LID layer and parameter names, control rule
  templates, engine feature names, engine descriptions and the key figures of scenario comparisons. They
  are in the context `core`. The program looks them up when it shows them (`coreText()` in
  `src/app/Language.h`); `src/app/CoreStrings.cpp` lists them for lupdate.

What stays in English on purpose:

- Names that can be typed into query expressions or that identify data: input column names
  (`Elevation`, `Diameter`, `InitLevel`, ...) and result variable names (`Pressure`, `Flow`, `Depth`,
  ...). They also name the columns of CSV and GIS exports.
- `.inp` section and keyword names (`[JUNCTIONS]`, `PRV`, `OPEN`, `DYNWAVE`, `RULE`, ...), element and
  object IDs, engine names and IDs, unit symbols (`m`, `L/s`, `mm/hr`).
- Messages composed by the libraries with IDs or line numbers inside (input file errors, rule check
  messages, compatibility details), and everything `mirab-cli` prints.
- The GPL notice in the About dialog (legal text).

Numbers in model files are always written and read with a decimal point, in every language. Numbers in
the interface are shown with Latin digits and a decimal point too, so values can be copied between the
interface, the text editor of the input file and query expressions. Spin boxes and dates follow the
operating system's regional settings, as before.

## Updating the files after code changes

1. If texts in `src/core` or `src/engine` changed, refresh the library list:
   `cmake --build <build-dir> --target update_core_strings`. The `i18n-core-strings` test fails when
   the list is out of date.
2. Refresh the `.ts` files from the sources:
   `cmake --build <build-dir> --target update_translations`. New texts appear as unfinished; texts no
   longer in the code are removed (lupdate runs with `-no-obsolete -locations none`).
3. Translate the new entries with Qt Linguist (`linguist translations/mirab_fa.ts`) or any editor that
   understands `.ts` files, then build and run `ctest -R "i18n|language"`.

The `i18n-ts-files` test checks that every file is well formed, has no obsolete entries, and keeps the
placeholders (`%1`, `%2`, `%n`) of each source text. It prints how many entries are finished per file.
The `i18n-ts-current` test fails when a file lacks a text of the program or keeps one the program no
longer has, that is when step 2 was forgotten after the sources changed. It refreshes copies of the
files in the build directory and leaves the files themselves alone.

## Reviewing

Entries a translator was not sure about are marked *unfinished* (Linguist shows them with a question
mark; in the file they carry `type="unfinished"`). Qt still uses an unfinished translation when it is
not empty. A reviewer who agrees with an entry marks it as done (Ctrl+Return in Linguist); a reviewer who
corrects one changes the text and marks it as done.

Guidelines:

- Keep placeholders, HTML tags, `.inp` keywords in capitals, file patterns (`*.inp`) and unit symbols.
- Keep the menu accelerators in Spanish, Portuguese, French, Italian and German: put `&` before a
  letter of the translated label. Chinese labels use the Latin letter in brackets, for example
  `文件(&F)` and `檔案(&F)`. Persian, Arabic and Hebrew labels have no `&`: in Persian and Arabic Qt draws the
  underlined letter on its own, which breaks the joined letters of the word, and right-to-left labels
  follow one rule (the check enforces this).
- Use the terms of the glossary below; consistency matters more than the perfect word.
- Persian: Persian letters (ی and ک, not ي and ك), zero-width non-joiners where they belong
  (`می‌شود`, `داده‌ها`), Latin digits for data.
- Arabic and Hebrew: Latin digits for data.
- Hebrew: no niqqud; the Hebrew maqaf (־) is not needed, a hyphen is fine (`תת-אגן`).
- French: a no-break space (U+00A0) before `:`, `;`, `?` and `!`, as in French typography
  (`Débit :`).
- Portuguese: Brazilian spelling and terms (`arquivo`, `vazão`, `tubulação`); it is also used on systems
  set to Portuguese of Portugal.
- Traditional Chinese: Taiwan usage (`檔案`, `設定`, `資料`, `預設`, `檢視`), full-width punctuation as in
  the Simplified Chinese file; it is also used on systems set to Hong Kong or Macau Chinese.
- German: nouns capitalised, compounds written as one word or with a hyphen (`LID-Maßnahme`), the
  formal "Sie" in messages.
- Persian, Arabic and Hebrew: a translation that starts with a Latin word, a number or a placeholder
  (`%1 ...`, `QGIS ...`) begins with an invisible RIGHT-TO-LEFT MARK (U+200F); otherwise Qt lays the
  sentence out from left to right. The `i18n-ts-files` test reports entries that need one.
- Sentence case and short labels, like the English interface.

## Adding a language

1. Add the code to `MIRAB_LANGUAGES` in `src/app/CMakeLists.txt`, to the list in
   `uiLanguages()` in `src/app/Language.cpp` (with the language's own name), to the right-to-left test in
   `installTranslations()` if needed, and to the `gui-language-*` tests in `tests/CMakeLists.txt`.
2. Create `translations/mirab_<code>.ts` with an empty `<TS version="2.1" language="<code>">` element and
   run `update_translations`.
3. Translate, build and run the tests. Qt's `qtbase_<code>.qm` is picked up from the Qt installation when
   it exists.

## Glossary

Key terms of water distribution (EPANET) and stormwater (SWMM) modeling as used in the interface.

| English | Persian | Arabic | Hebrew | Spanish | Portuguese (Brazil) | French | Italian | German | Chinese (Simplified) | Chinese (Traditional) |
|---|---|---|---|---|---|---|---|---|---|---|
| Node | گره | عقدة | צומת | Nodo | Nó | Nœud | Nodo | Knoten | 节点 | 節點 |
| Link | پیوند | وصلة | מקטע | Línea | Trecho | Tronçon | Ramo | Verbindung | 管段 | 管段 |
| Junction | اتصال | تقاطع | צומת חיבור | Unión | Junção | Nœud de jonction | Giunzione | Knotenpunkt | 连接节点 | 連接節點 |
| Reservoir | منبع | خزان مصدر | מאגר | Embalse | Reservatório de nível fixo | Bâche | Serbatoio a livello fisso | Reservoir | 水库 | 水庫 |
| Tank | مخزن | خزان | מיכל | Depósito | Reservatório | Réservoir | Serbatoio | Behälter | 水池 | 水池 |
| Pipe | لوله | أنبوب | צינור | Tubería | Tubulação | Tuyau | Condotta | Rohrleitung | 管道 | 管線 |
| Pump | پمپ | مضخة | משאבה | Bomba | Bomba | Pompe | Pompa | Pumpe | 水泵 | 抽水機 |
| Valve | شیر | صمام | מגוף | Válvula | Válvula | Vanne | Valvola | Ventil | 阀门 | 閥門 |
| Emitter | افشانه | باعث | מתז | Emisor | Emissor | Émetteur | Erogatore | Emitter | 射流器 | 噴嘴 |
| Pressure | فشار | الضغط | לחץ | Presión | Pressão | Pression | Pressione | Druck | 压力 | 壓力 |
| Head | هد | الضاغط | עומד | Altura piezométrica | Carga hidráulica | Charge | Carico piezometrico | Druckhöhe | 水头 | 水頭 |
| Headloss | افت هد | فاقد الضاغط | הפסד עומד | Pérdida de carga | Perda de carga | Perte de charge | Perdita di carico | Druckhöhenverlust | 水头损失 | 水頭損失 |
| Flow | دبی | التدفق | ספיקה | Caudal | Vazão | Débit | Portata | Durchfluss | 流量 | 流量 |
| Demand | تقاضا | الطلب | צריכה | Demanda | Demanda | Demande | Domanda | Bedarf | 需水量 | 需水量 |
| Elevation | تراز | المنسوب | רום | Cota | Cota | Altitude | Quota | Höhe | 高程 | 高程 |
| Roughness | زبری | الخشونة | חספוס | Rugosidad | Rugosidade | Rugosité | Scabrezza | Rauheit | 粗糙系数 | 粗糙係數 |
| Minor loss | افت جزئی | الفاقد الثانوي | הפסד מקומי | Pérdidas menores | Perda localizada | Pertes singulières | Perdite localizzate | Einzelverlust | 局部损失 | 局部損失 |
| Velocity | سرعت | السرعة | מהירות | Velocidad | Velocidade | Vitesse | Velocità | Fließgeschwindigkeit | 流速 | 流速 |
| Pattern | الگو | نمط | תבנית | Patrón | Padrão temporal | Courbe de modulation | Modulazione | Zeitmuster | 模式 | 時間模式 |
| Curve | منحنی | منحنى | עקומה | Curva | Curva | Courbe | Curva | Kennlinie | 曲线 | 曲線 |
| Time series | سری زمانی | سلسلة زمنية | סדרה עתית | Serie temporal | Série temporal | Série temporelle | Serie temporale | Zeitreihe | 时间序列 | 時間序列 |
| Water quality | کیفیت آب | جودة المياه | איכות מים | Calidad del agua | Qualidade da água | Qualité de l'eau | Qualità dell'acqua | Wasserqualität | 水质 | 水質 |
| Water age | عمر آب | عمر المياه | גיל המים | Edad del agua | Idade da água | Âge de l'eau | Età dell'acqua | Wasseralter | 水龄 | 水齡 |
| Pressure-driven demand | تقاضای وابسته به فشار | الطلب المعتمد على الضغط | צריכה תלוית לחץ | Demanda dependiente de la presión | Demanda dependente da pressão | Demande dépendante de la pression | Domanda dipendente dalla pressione | Druckabhängiger Bedarf | 压力驱动需水量 | 壓力驅動需水量 |
| Leakage | نشت | التسرب | דליפה | Fugas | Vazamentos | Fuites | Perdite idriche | Leckage | 漏损 | 漏水 |
| Fire flow | دبی آتش‌نشانی | تدفق الإطفاء | ספיקת כיבוי אש | Caudal contra incendios | Vazão de incêndio | Débit incendie | Portata antincendio | Löschwassermenge | 消防流量 | 消防流量 |
| Subcatchment | زیرحوضه | حوض فرعي | תת-אגן | Subcuenca | Sub-bacia | Sous-bassin versant | Sottobacino | Teileinzugsgebiet | 子汇水区 | 子集水區 |
| Rain gage | باران‌سنج | مقياس المطر | מד גשם | Pluviómetro | Pluviômetro | Pluviomètre | Pluviometro | Regenmesser | 雨量计 | 雨量站 |
| Conduit | مجرا | قناة | מוביל | Conducto | Conduto | Conduite | Collettore | Haltung | 管渠 | 管渠 |
| Outfall | نقطه تخلیه | مَصبّ | מוצא | Vertido | Exutório | Exutoire | Scarico finale | Auslauf | 排放口 | 放流口 |
| Storage unit | واحد ذخیره | وحدة تخزين | יחידת אגירה | Depósito de almacenamiento | Unidade de armazenamento | Ouvrage de stockage | Vasca di accumulo | Speicherbauwerk | 蓄水单元 | 蓄水單元 |
| Divider | مقسم جریان | مقسم التدفق | מפצל זרימה | Divisor | Divisor de vazão | Répartiteur | Partitore | Abflussteiler | 分流器 | 分流器 |
| Orifice | روزنه | فتحة | פתח | Orificio | Orifício | Orifice | Luce | Blende | 孔口 | 孔口 |
| Weir | سرریز | هدّار | גלשן | Vertedero | Vertedor | Déversoir | Stramazzo | Wehr | 堰 | 堰 |
| Outlet | دریچه خروجی | مخرج | מבנה יציאה | Salida | Estrutura de saída | Ouvrage de sortie | Manufatto di scarico | Auslassbauwerk | 出水口 | 出水口 |
| Inlet | آبگیر | مدخل تصريف | קולטן | Imbornal | Boca de lobo | Avaloir | Caditoia | Straßenablauf | 雨水口 | 進水口 |
| Street | خیابان | شارع | רחוב | Calle | Rua | Rue | Strada | Straße | 街道 | 街道 |
| Transect | مقطع عرضی | مقطع عرضي | חתך רוחב | Sección transversal | Seção transversal | Profil en travers | Sezione trasversale | Querprofil | 横断面 | 橫斷面 |
| Land use | کاربری اراضی | استخدام الأراضي | שימוש קרקע | Uso del suelo | Uso do solo | Occupation du sol | Uso del suolo | Landnutzung | 土地利用 | 土地利用 |
| Pollutant | آلاینده | ملوِّث | מזהם | Contaminante | Poluente | Polluant | Inquinante | Schadstoff | 污染物 | 污染物 |
| Aquifer | آبخوان | خزان جوفي | אקוויפר | Acuífero | Aquífero | Aquifère | Acquifero | Grundwasserleiter | 含水层 | 含水層 |
| Snow pack | توده برف | الغطاء الثلجي | מעטה שלג | Manto de nieve | Manto de neve | Manteau neigeux | Manto nevoso | Schneedecke | 积雪 | 積雪 |
| Unit hydrograph | هیدروگراف واحد | الهيدروغراف الوحدوي | הידרוגרף יחידה | Hidrograma unitario | Hidrograma unitário | Hydrogramme unitaire | Idrogramma unitario | Einheitsganglinie | 单位线 | 單位歷線 |
| LID control | کنترل LID | ضابط LID | אמצעי LID | Control LID | Controle LID | Ouvrage LID | Intervento LID | LID-Maßnahme | LID 控制 | LID 設施 |
| Control rules | قواعد کنترلی | قواعد التحكم | כללי בקרה | Reglas de control | Regras de controle | Règles de contrôle | Regole di controllo | Steuerregeln | 控制规则 | 控制規則 |
| Runoff | رواناب | الجريان السطحي | נגר | Escorrentía | Escoamento superficial | Ruissellement | Deflusso superficiale | Oberflächenabfluss | 径流 | 逕流 |
| Infiltration | نفوذ | الارتشاح | חלחול | Infiltración | Infiltração | Infiltration | Infiltrazione | Versickerung | 入渗 | 入滲 |
| Rainfall | بارش | الهطول | משקעים | Precipitación | Precipitação | Précipitations | Precipitazione | Niederschlag | 降雨 | 降雨 |
| Evaporation | تبخیر | التبخر | התאדות | Evaporación | Evaporação | Évaporation | Evaporazione | Verdunstung | 蒸发 | 蒸發 |
| Groundwater | آب زیرزمینی | المياه الجوفية | מי תהום | Aguas subterráneas | Água subterrânea | Eaux souterraines | Acque sotterranee | Grundwasser | 地下水 | 地下水 |
| Snowmelt | ذوب برف | ذوبان الثلوج | הפשרת שלג | Fusión de nieve | Degelo | Fonte des neiges | Scioglimento nivale | Schneeschmelze | 融雪 | 融雪 |
| Flooding | آب‌گرفتگی | الغمر | הצפה | Inundación | Alagamento | Débordement | Allagamento | Überstau | 溢流 | 溢淹 |
| Depth | عمق | العمق | עומק | Calado | Profundidade | Hauteur d'eau | Tirante idrico | Wassertiefe | 水深 | 水深 |
| Routing | روندیابی جریان | تتبع الجريان | ניתוב זרימה | Cálculo hidráulico | Propagação de vazões | Propagation des écoulements | Propagazione delle portate | Abflusstransport | 流量演算 | 流量演算 |
| Dynamic wave | موج دینامیکی | الموجة الديناميكية | גל דינמי | Onda dinámica | Onda dinâmica | Onde dynamique | Onda dinamica | Dynamische Welle | 动力波 | 動力波 |
| Kinematic wave | موج سینماتیکی | الموجة الحركية | גל קינמטי | Onda cinemática | Onda cinemática | Onde cinématique | Onda cinematica | Kinematische Welle | 运动波 | 運動波 |
| Simulation | شبیه‌سازی | المحاكاة | סימולציה | Simulación | Simulação | Simulation | Simulazione | Simulation | 模拟 | 模擬 |
| Engine | موتور محاسباتی | محرك الحساب | מנוע חישוב | Motor de cálculo | Motor de cálculo | Moteur de calcul | Motore di calcolo | Rechenkern | 计算引擎 | 計算引擎 |
| Time step | گام زمانی | الخطوة الزمنية | צעד זמן | Paso de tiempo | Passo de tempo | Pas de temps | Passo temporale | Zeitschritt | 时间步长 | 時間間距 |
| Reporting period | دوره گزارش | فترة التقرير | תקופת דיווח | Periodo de informe | Período de relatório | Période de rapport | Periodo di report | Berichtszeitraum | 报告时段 | 報表時段 |
| Results | نتایج | النتائج | תוצאות | Resultados | Resultados | Résultats | Risultati | Ergebnisse | 结果 | 結果 |
| Report | گزارش | تقرير | דוח | Informe | Relatório | Rapport | Report | Bericht | 报告 | 報表 |
| Scenario | سناریو | سيناريو | תרחיש | Escenario | Cenário | Scénario | Scenario | Szenario | 方案 | 情境 |
| Calibration | واسنجی | المعايرة | כיול | Calibración | Calibração | Calage | Calibrazione | Kalibrierung | 率定 | 檢定 |
| Observed data | داده‌های مشاهداتی | البيانات المرصودة | נתונים נמדדים | Datos observados | Dados observados | Données observées | Dati osservati | Messdaten | 观测数据 | 觀測資料 |
| Goodness of fit | نیکویی برازش | جودة المطابقة | טיב התאמה | Bondad de ajuste | Qualidade do ajuste | Qualité d'ajustement | Bontà di adattamento | Anpassungsgüte | 拟合优度 | 適合度 |
| Resilience | تاب‌آوری | المرونة | חוסן | Resiliencia | Resiliência | Résilience | Resilienza | Resilienz | 韧性 | 韌性 |
| Criticality | بحرانی بودن | الحرجية | קריטיות | Criticidad | Criticidade | Criticité | Criticità | Kritikalität | 关键性 | 關鍵性 |
| Upstream / downstream | بالادست / پایین‌دست | المنبع / المصب | במעלה הזרם / במורד הזרם | Aguas arriba / aguas abajo | Montante / jusante | Amont / aval | Monte / valle | Oberstrom / Unterstrom | 上游 / 下游 | 上游 / 下游 |
| Profile | نیمرخ طولی | المقطع الطولي | חתך אורך | Perfil longitudinal | Perfil longitudinal | Profil en long | Profilo longitudinale | Längsschnitt | 纵断面 | 縱剖面 |
| Basemap | نقشه پایه | خريطة الأساس | מפת רקע | Mapa base | Mapa de fundo | Fond de carte | Mappa di base | Hintergrundkarte | 底图 | 底圖 |
| Coordinate system | سیستم مختصات | نظام الإحداثيات | מערכת קואורדינטות | Sistema de coordenadas | Sistema de coordenadas | Système de coordonnées | Sistema di coordinate | Koordinatensystem | 坐标系 | 座標系統 |
| Elevation model (DEM) | مدل رقومی ارتفاع | نموذج الارتفاع الرقمي | מודל רום דיגיטלי | Modelo digital de elevaciones | Modelo digital de elevação | Modèle numérique de terrain | Modello digitale del terreno | Digitales Geländemodell | 数字高程模型 | 數值高程模型 |
| Layer | لایه | طبقة | שכבה | Capa | Camada | Couche | Livello | Ebene | 图层 | 圖層 |
| Legend | راهنمای نقشه | مفتاح الخريطة | מקרא | Leyenda | Legenda | Légende | Legenda | Legende | 图例 | 圖例 |
| Print layout | صفحه‌آرایی چاپ | تخطيط الطباعة | פריסת הדפסה | Diseño de impresión | Layout de impressão | Mise en page | Layout di stampa | Drucklayout | 打印布局 | 列印版面 |
| Input file | فایل ورودی | ملف الإدخال | קובץ קלט | Archivo de entrada | Arquivo de entrada | Fichier d'entrée | File di input | Eingabedatei | 输入文件 | 輸入檔 |
| Query | پرس‌وجو | استعلام | שאילתה | Consulta | Consulta | Requête | Interrogazione | Abfrage | 查询 | 查詢 |
| Wastewater | فاضلاب | مياه الصرف الصحي | שפכים | Aguas residuales | Águas residuárias | Eaux usées | Acque reflue | Abwasser | 污水 | 污水 |
| Sewer | فاضلاب‌رو | الصرف الصحي | ביוב | Alcantarillado | Esgoto | Égout | Fognatura | Kanal | 污水管 | 污水下水道 |
| Sewer network | شبکه فاضلاب | شبكة الصرف الصحي | רשת ביוב | Red de alcantarillado | Rede de esgoto | Réseau d'assainissement | Rete fognaria | Kanalnetz | 污水管网 | 污水下水道系統 |
| Manhole | آدم‌رو | غرفة تفتيش | שוחה | Pozo de registro | Poço de visita | Regard | Pozzetto | Schacht | 检查井 | 人孔 |
| Dry-weather flow | جریان هوای خشک | تدفق الطقس الجاف | ספיקת מזג אוויר יבש | Caudal de tiempo seco | Vazão de tempo seco | Débit de temps sec | Portata in tempo secco | Trockenwetterabfluss | 旱流 | 旱天流量 |
| Return factor | ضریب بازگشت | معامل الإرجاع | מקדם החזרה | Coeficiente de retorno | Coeficiente de retorno | Coefficient de rejet | Coefficiente di restituzione | Abwasseranteil | 排放系数 | 回歸係數 |
| Water use | مصرف آب | استهلاك المياه | צריכת מים | Consumo de agua | Consumo de água | Consommation d'eau | Consumo idrico | Wasserverbrauch | 用水量 | 用水量 |
| Demand category | دسته تقاضا | فئة الطلب | קטגוריית צריכה | Categoría de demanda | Categoria de demanda | Catégorie de demande | Categoria di domanda | Bedarfskategorie | 需水类别 | 用水類別 |
| Service area | محدوده خدمات | منطقة الخدمة | אזור שירות | Área de servicio | Área de atendimento | Zone desservie | Area servita | Versorgungsgebiet | 服务区域 | 服務區域 |
| Inflow | جریان ورودی | التدفق الداخل | זרימה נכנסת | Caudal de entrada | Vazão afluente | Apport | Afflusso | Zufluss | 入流 | 入流 |
