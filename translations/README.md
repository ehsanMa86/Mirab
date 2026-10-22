# Translations

Mirab's interface is translated with Qt Linguist. This folder holds one source file per language:

| File | Language | Direction |
|---|---|---|
| `mirab_fa.ts` | Persian (فارسی) | right to left |
| `mirab_ar.ts` | Arabic (العربية) | right to left |
| `mirab_es.ts` | Spanish (Español) | left to right |
| `mirab_zh_CN.ts` | Simplified Chinese (简体中文) | left to right |

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

## Reviewing

Entries a translator was not sure about are marked *unfinished* (Linguist shows them with a question
mark; in the file they carry `type="unfinished"`). Qt still uses an unfinished translation when it is
not empty. A reviewer who agrees with an entry marks it as done (Ctrl+Return in Linguist); a reviewer who
corrects one changes the text and marks it as done.

Guidelines:

- Keep placeholders, HTML tags, `.inp` keywords in capitals, file patterns (`*.inp`) and unit symbols.
- Keep the menu accelerators in Spanish: put `&` before a letter of the translated label. Chinese labels
  use the Latin letter in brackets, for example `文件(&F)`. Persian and Arabic labels have no `&`: Qt
  draws the underlined letter on its own, which breaks the joined letters of the word (the check
  enforces this).
- Use the terms of the glossary below; consistency matters more than the perfect word.
- Persian: Persian letters (ی and ک, not ي and ك), zero-width non-joiners where they belong
  (`می‌شود`, `داده‌ها`), Latin digits for data.
- Arabic: Latin digits for data.
- Persian and Arabic: a translation that starts with a Latin word, a number or a placeholder
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

| English | Persian | Arabic | Spanish | Chinese |
|---|---|---|---|---|
| Node | گره | عقدة | Nodo | 节点 |
| Link | پیوند | وصلة | Línea | 管段 |
| Junction | اتصال | تقاطع | Unión | 连接节点 |
| Reservoir | منبع | خزان مصدر | Embalse | 水库 |
| Tank | مخزن | خزان | Depósito | 水池 |
| Pipe | لوله | أنبوب | Tubería | 管道 |
| Pump | پمپ | مضخة | Bomba | 水泵 |
| Valve | شیر | صمام | Válvula | 阀门 |
| Emitter | افشانه | باعث | Emisor | 射流器 |
| Pressure | فشار | الضغط | Presión | 压力 |
| Head | هد | الضاغط | Altura piezométrica | 水头 |
| Headloss | افت هد | فاقد الضاغط | Pérdida de carga | 水头损失 |
| Flow | دبی | التدفق | Caudal | 流量 |
| Demand | تقاضا | الطلب | Demanda | 需水量 |
| Elevation | تراز | المنسوب | Cota | 高程 |
| Roughness | زبری | الخشونة | Rugosidad | 粗糙系数 |
| Minor loss | افت جزئی | الفاقد الثانوي | Pérdidas menores | 局部损失 |
| Velocity | سرعت | السرعة | Velocidad | 流速 |
| Pattern | الگو | نمط | Patrón | 模式 |
| Curve | منحنی | منحنى | Curva | 曲线 |
| Time series | سری زمانی | سلسلة زمنية | Serie temporal | 时间序列 |
| Water quality | کیفیت آب | جودة المياه | Calidad del agua | 水质 |
| Water age | عمر آب | عمر المياه | Edad del agua | 水龄 |
| Pressure-driven demand | تقاضای وابسته به فشار | الطلب المعتمد على الضغط | Demanda dependiente de la presión | 压力驱动需水量 |
| Leakage | نشت | التسرب | Fugas | 漏损 |
| Fire flow | دبی آتش‌نشانی | تدفق الإطفاء | Caudal contra incendios | 消防流量 |
| Subcatchment | زیرحوضه | حوض فرعي | Subcuenca | 子汇水区 |
| Rain gage | باران‌سنج | مقياس المطر | Pluviómetro | 雨量计 |
| Conduit | مجرا | قناة | Conducto | 管渠 |
| Outfall | نقطه تخلیه | مَصبّ | Vertido | 排放口 |
| Storage unit | واحد ذخیره | وحدة تخزين | Depósito de almacenamiento | 蓄水单元 |
| Divider | مقسم جریان | مقسم التدفق | Divisor | 分流器 |
| Orifice | روزنه | فتحة | Orificio | 孔口 |
| Weir | سرریز | هدّار | Vertedero | 堰 |
| Outlet | دریچه خروجی | مخرج | Salida | 出水口 |
| Inlet | آبگیر | مدخل تصريف | Imbornal | 雨水口 |
| Street | خیابان | شارع | Calle | 街道 |
| Transect | مقطع عرضی | مقطع عرضي | Sección transversal | 横断面 |
| Land use | کاربری اراضی | استخدام الأراضي | Uso del suelo | 土地利用 |
| Pollutant | آلاینده | ملوِّث | Contaminante | 污染物 |
| Aquifer | آبخوان | خزان جوفي | Acuífero | 含水层 |
| Snow pack | توده برف | الغطاء الثلجي | Manto de nieve | 积雪 |
| Unit hydrograph | هیدروگراف واحد | الهيدروغراف الوحدوي | Hidrograma unitario | 单位线 |
| LID control | کنترل LID | ضابط LID | Control LID | LID 控制 |
| Control rules | قواعد کنترلی | قواعد التحكم | Reglas de control | 控制规则 |
| Runoff | رواناب | الجريان السطحي | Escorrentía | 径流 |
| Infiltration | نفوذ | الارتشاح | Infiltración | 入渗 |
| Rainfall | بارش | الهطول | Precipitación | 降雨 |
| Evaporation | تبخیر | التبخر | Evaporación | 蒸发 |
| Groundwater | آب زیرزمینی | المياه الجوفية | Aguas subterráneas | 地下水 |
| Snowmelt | ذوب برف | ذوبان الثلوج | Fusión de nieve | 融雪 |
| Flooding | آب‌گرفتگی | الغمر | Inundación | 溢流 |
| Depth | عمق | العمق | Calado | 水深 |
| Routing | روندیابی جریان | تتبع الجريان | Cálculo hidráulico | 流量演算 |
| Dynamic wave | موج دینامیکی | الموجة الديناميكية | Onda dinámica | 动力波 |
| Kinematic wave | موج سینماتیکی | الموجة الحركية | Onda cinemática | 运动波 |
| Simulation | شبیه‌سازی | المحاكاة | Simulación | 模拟 |
| Engine | موتور محاسباتی | محرك الحساب | Motor de cálculo | 计算引擎 |
| Time step | گام زمانی | الخطوة الزمنية | Paso de tiempo | 时间步长 |
| Reporting period | دوره گزارش | فترة التقرير | Periodo de informe | 报告时段 |
| Results | نتایج | النتائج | Resultados | 结果 |
| Report | گزارش | تقرير | Informe | 报告 |
| Scenario | سناریو | سيناريو | Escenario | 方案 |
| Calibration | واسنجی | المعايرة | Calibración | 率定 |
| Observed data | داده‌های مشاهداتی | البيانات المرصودة | Datos observados | 观测数据 |
| Goodness of fit | نیکویی برازش | جودة المطابقة | Bondad de ajuste | 拟合优度 |
| Resilience | تاب‌آوری | المرونة | Resiliencia | 韧性 |
| Criticality | بحرانی بودن | الحرجية | Criticidad | 关键性 |
| Upstream / downstream | بالادست / پایین‌دست | المنبع / المصب | Aguas arriba / aguas abajo | 上游 / 下游 |
| Profile | نیمرخ طولی | المقطع الطولي | Perfil longitudinal | 纵断面 |
| Basemap | نقشه پایه | خريطة الأساس | Mapa base | 底图 |
| Coordinate system | سیستم مختصات | نظام الإحداثيات | Sistema de coordenadas | 坐标系 |
| Elevation model (DEM) | مدل رقومی ارتفاع | نموذج الارتفاع الرقمي | Modelo digital de elevaciones | 数字高程模型 |
| Layer | لایه | طبقة | Capa | 图层 |
| Legend | راهنمای نقشه | مفتاح الخريطة | Leyenda | 图例 |
| Print layout | صفحه‌آرایی چاپ | تخطيط الطباعة | Diseño de impresión | 打印布局 |
| Input file | فایل ورودی | ملف الإدخال | Archivo de entrada | 输入文件 |
| Query | پرس‌وجو | استعلام | Consulta | 查询 |
| Wastewater | فاضلاب | مياه الصرف الصحي | Aguas residuales | 污水 |
| Sewer | فاضلاب‌رو | الصرف الصحي | Alcantarillado | 污水管 |
| Sewer network | شبکه فاضلاب | شبكة الصرف الصحي | Red de alcantarillado | 污水管网 |
| Manhole | آدم‌رو | غرفة تفتيش | Pozo de registro | 检查井 |
| Dry-weather flow | جریان هوای خشک | تدفق الطقس الجاف | Caudal de tiempo seco | 旱流 |
| Return factor | ضریب بازگشت | معامل الإرجاع | Coeficiente de retorno | 排放系数 |
| Water use | مصرف آب | استهلاك المياه | Consumo de agua | 用水量 |
| Demand category | دسته تقاضا | فئة الطلب | Categoría de demanda | 需水类别 |
| Service area | محدوده خدمات | منطقة الخدمة | Área de servicio | 服务区域 |
| Inflow | جریان ورودی | التدفق الداخل | Caudal de entrada | 入流 |
