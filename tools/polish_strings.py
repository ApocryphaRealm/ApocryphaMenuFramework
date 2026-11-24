# The 2.5.0 polish pass's strings, in every language AMF ships (rule 66). Run once to bring every translation file up
# to date:  python -I tools\polish_strings.py     (--check: report only, exit 1 when a file would change).
# Files stay UTF-16 LE with a BOM and CRLF, as SkyUI's and SKSE's translation files are.
#
# Three kinds of change:
#   SET        a new key, or new wording for an existing key, written out in every language;
#   SWAP       an existing key whose translation keeps its words and only trades a fixed name for a "%s" the code fills
#              in - the on-screen keyboard's hint named X, Y and RT although those buttons can be rebound, and the manual
#              named F1 although the menu key can be. Done per language on the translation that is already there, and
#              refused when the name is not found exactly as often as expected;
#   REMOVE     a key the code no longer asks for.
# Run twice it changes nothing: a SWAP whose value already holds "%s" is left alone.
import io
import os
import sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist', 'Interface', 'Translations')
LANGS = ['english', 'german', 'french', 'spanish', 'italian', 'polish', 'czech', 'russian', 'japanese', 'korean', 'chinese']

SET = {
    # A10: the theme help named two themes; eleven ship
    'AMF_ThemeHelp': {
        'english': 'A theme sets the menu\'s frame art, shapes and colours together. "Skyrim" is the Nordic knotwork frame, "Untarnished" is plain lines with no art, and each of the other themes brings its own art.',
        'german': 'Ein Design legt Rahmengrafik, Formen und Farben des Menüs gemeinsam fest. "Skyrim" ist der nordische Knotenwerk-Rahmen, "Untarnished" hat klare Linien ohne Rahmengrafik, und jedes andere Design bringt seine eigene Grafik mit.',
        'french': "Un thème règle ensemble le cadre, les formes et les couleurs du menu. « Skyrim » est le cadre nordique à entrelacs, « Untarnished » des lignes nettes sans cadre décoratif, et chacun des autres thèmes apporte son propre décor.",
        'spanish': 'Un tema fija a la vez el marco, las formas y los colores del menú. "Skyrim" es el marco nórdico de entrelazado, "Untarnished" son líneas limpias sin marco decorativo, y cada uno de los demás temas trae su propio arte.',
        'italian': 'Un tema imposta insieme cornice, forme e colori del menu. "Skyrim" è la cornice nordica a intrecci, "Untarnished" ha linee pulite senza cornice decorativa, e ognuno degli altri temi porta la propria grafica.',
        'polish': 'Motyw ustawia razem ramę, kształty i kolory menu. „Skyrim” to nordycka rama z plecionką, „Untarnished” to czyste linie bez ramy, a każdy z pozostałych motywów ma własną grafikę.',
        'czech': 'Motiv nastavuje společně rám, tvary a barvy nabídky. „Skyrim“ je severský pletencový rám, „Untarnished“ má čisté linky bez rámu a každý další motiv přináší vlastní grafiku.',
        'russian': 'Тема задаёт рамку, формы и цвета меню вместе. «Skyrim» - нордическая узорная рамка, «Untarnished» - чистые линии без рамки, а у каждой из остальных тем своё оформление.',
        'japanese': 'テーマは、メニューの枠の装飾・形・色をまとめて決めます。「Skyrim」はノルド風の組紐模様の枠、「Untarnished」は飾り枠のないすっきりした線で、ほかのテーマにはそれぞれ独自の装飾があります。',
        'korean': "테마는 메뉴의 테두리 장식, 모양, 색을 한꺼번에 정합니다. 'Skyrim'은 노르드풍 매듭 무늬 테두리, 'Untarnished'는 장식 테두리가 없는 깔끔한 선이며, 나머지 테마는 저마다 고유한 장식을 씁니다.",
        'chinese': '主题会同时决定菜单的边框美术、形状和颜色。“Skyrim”是诺德风格的绳结边框，“Untarnished”是线条简洁、没有边框美术的样式，其余每个主题都有各自的美术。',
    },
    # A10: the manual described the two A-Z / Z-A switches of before 2.1.1
    'AMF_ManList2': {
        'english': 'The tick box beside Mods turns alphabetical order on, and the switch next to it picks A-Z or Z-A. Untick the box and the list goes back to the order you arranged it in.',
        'german': 'Das Kästchen neben Mods schaltet die alphabetische Reihenfolge ein, und der Schalter daneben wählt A-Z oder Z-A. Ohne Haken kehrt die Liste zu der Reihenfolge zurück, in der du sie angeordnet hast.',
        'french': "La case à côté de Mods active l'ordre alphabétique, et l'interrupteur voisin choisit A-Z ou Z-A. Décochez la case et la liste retrouve l'ordre dans lequel vous l'aviez arrangée.",
        'spanish': 'La casilla junto a Mods activa el orden alfabético, y el interruptor de al lado elige A-Z o Z-A. Desmarca la casilla y la lista vuelve al orden en que la organizaste.',
        'italian': "La casella accanto a Mod attiva l'ordine alfabetico, e l'interruttore vicino sceglie A-Z o Z-A. Togli il segno e l'elenco torna all'ordine in cui l'avevi sistemato.",
        'polish': 'Pole wyboru obok Modów włącza kolejność alfabetyczną, a przełącznik obok wybiera A-Z lub Z-A. Odznacz pole, a lista wróci do kolejności, w jakiej ją ułożyłeś.',
        'czech': 'Zaškrtávací pole vedle Modů zapíná abecední řazení a přepínač vedle něj volí A-Z nebo Z-A. Zrušte zaškrtnutí a seznam se vrátí do pořadí, které jste si sami uspořádali.',
        'russian': 'Флажок рядом с Модами включает алфавитный порядок, а переключатель рядом выбирает A-Z или Z-A. Снимите флажок - и список вернётся к порядку, в котором вы его расставили.',
        'japanese': 'MODの横のチェックボックスでアルファベット順をオンにし、その隣のスイッチでA-ZかZ-Aを選びます。チェックを外すと、自分で並べた順序に戻ります。',
        'korean': '모드 옆의 체크 상자가 알파벳순 정렬을 켜고, 그 옆의 스위치가 A-Z 또는 Z-A를 고릅니다. 체크를 끄면 목록은 직접 정리한 순서로 돌아갑니다.',
        'chinese': '“模组”旁边的复选框用于开启字母顺序，旁边的开关用于选择 A-Z 或 Z-A。取消勾选后，列表会恢复为你自行排列的顺序。',
    },
    # A10: the manual described per-way window memory and a switch per kind of drag; both were replaced
    'AMF_ManLook5': {
        'english': 'Window: Move style sets how it is dragged - anywhere, by its top edge, or not at all - and Resize style how its edges behave. It opens where you left it. See-through lets the game show behind it. Settings -> Appearance -> Window.',
        'german': 'Fenster: Die Verschiebeart legt fest, wie es gezogen wird - überall, an der oberen Kante oder gar nicht - und die Größenänderung, wie sich seine Ränder verhalten. Es öffnet sich dort, wo du es gelassen hast. Durchscheinend lässt das Spiel dahinter sehen. Einstellungen -> Aussehen -> Fenster.',
        'french': "Fenêtre : le mode de déplacement règle comment on la fait glisser - partout, par son bord supérieur ou pas du tout - et le mode de redimensionnement comment ses bords se comportent. Elle s'ouvre là où vous l'avez laissée. Fenêtre transparente laisse voir le jeu derrière. Paramètres -> Apparence -> Fenêtre.",
        'spanish': 'Ventana: el modo de mover fija cómo se arrastra - por cualquier parte, por el borde superior o de ningún modo - y el modo de redimensionar cómo se comportan sus bordes. Se abre donde la dejaste. Ventana transparente deja ver el juego detrás. Ajustes -> Apariencia -> Ventana.',
        'italian': 'Finestra: la modalità di spostamento stabilisce come si trascina - ovunque, dal bordo superiore o per niente - e la modalità di ridimensionamento come si comportano i bordi. Si apre dove l\'hai lasciata. Finestra trasparente lascia vedere il gioco dietro. Impostazioni -> Aspetto -> Finestra.',
        'polish': 'Okno: sposób przesuwania określa, jak się je przeciąga - za dowolne miejsce, za górną krawędź albo wcale - a sposób zmiany rozmiaru, jak zachowują się krawędzie. Otwiera się tam, gdzie je zostawisz. Przezroczyste okno pokazuje grę za nim. Ustawienia -> Wygląd -> Okno.',
        'czech': 'Okno: způsob přesunu určuje, jak se táhne - kdekoli, za horní okraj, nebo vůbec - a způsob změny velikosti, jak se chovají okraje. Otevře se tam, kde jste ho nechali. Průsvitné okno nechá prosvítat hru. Nastavení -> Vzhled -> Okno.',
        'russian': 'Окно: способ перемещения задаёт, как его перетаскивать - за любое место, за верхний край или никак, - а способ изменения размера - как ведут себя края. Оно открывается там, где вы его оставили. Полупрозрачное окно показывает игру за ним. Настройки -> Внешний вид -> Окно.',
        'japanese': 'ウィンドウ：「移動の方法」でドラッグのしかた（どこでも、上端のみ、移動しない）を、「サイズ変更の方法」で辺の動き方を決めます。最後に置いた位置で開きます。半透明ウィンドウでは後ろのゲームが透けて見えます。設定 -> 外観 -> ウィンドウ。',
        'korean': '창: 이동 방식은 창을 끄는 방법(어디서나, 위쪽 가장자리만, 이동 안 함)을, 크기 조절 방식은 가장자리가 움직이는 방법을 정합니다. 마지막에 둔 자리에서 열립니다. 반투명 창은 뒤의 게임이 비쳐 보이게 합니다. 설정 -> 외형 -> 창.',
        'chinese': '窗口：“移动方式”决定如何拖动它——任意位置、仅顶部边缘或不可移动；“调整大小方式”决定边缘如何变化。窗口会在你上次留下的位置打开。半透明窗口可让游戏从后面透出。设置 -> 外观 -> 窗口。',
    },
    # A10: "sits in the middle" stopped being true when the window became movable
    'AMF_WindowSizeHelp': {
        'english': 'The first time, the menu opens in the middle of the screen, wide enough to show the menu names and the page in full - the same from its key and from the System menu row. The size you drag it to is kept.',
        'german': 'Beim ersten Mal öffnet sich das Menü in der Mitte des Bildschirms, breit genug, um die Menünamen und die Seite vollständig zu zeigen - über seine Taste ebenso wie über die Zeile im Systemmenü. Die Größe, auf die du es ziehst, bleibt erhalten.',
        'french': "La première fois, le menu s'ouvre au centre de l'écran, assez large pour montrer en entier les noms des menus et la page - par sa touche comme par la ligne du menu Système. La taille que vous lui donnez est conservée.",
        'spanish': 'La primera vez, el menú se abre en el centro de la pantalla, lo bastante ancho para mostrar completos los nombres de los menús y la página, tanto con su tecla como desde la fila del menú Sistema. El tamaño al que lo arrastres se conserva.',
        'italian': 'La prima volta il menu si apre al centro dello schermo, abbastanza largo da mostrare per intero i nomi dei menu e la pagina, sia con il suo tasto sia dalla riga del menu Sistema. La dimensione a cui lo trascini viene mantenuta.',
        'polish': 'Za pierwszym razem menu otwiera się na środku ekranu, na tyle szerokie, by nazwy menu i strona były widoczne w całości - zarówno z klawisza, jak i z wiersza w menu Systemu. Rozmiar, do którego je przeciągniesz, zostaje zapamiętany.',
        'czech': 'Poprvé se nabídka otevře uprostřed obrazovky, dost široká, aby byly názvy nabídek i stránka vidět celé - z klávesy i z řádku v systémové nabídce. Velikost, na kterou ji roztáhnete, se zachová.',
        'russian': 'В первый раз меню открывается в центре экрана, достаточно широким, чтобы названия меню и страница были видны полностью, - и по клавише, и из строки системного меню. Размер, до которого вы его растянете, сохраняется.',
        'japanese': '初回は、キーからでもシステムメニューの行からでも、メニュー名とページ全体が見える幅で画面中央に開きます。ドラッグして変えた大きさは保存されます。',
        'korean': '처음에는 키로 열든 시스템 메뉴 항목으로 열든 메뉴 이름과 페이지가 모두 보이는 너비로 화면 중앙에 열립니다. 끌어서 바꾼 크기는 유지됩니다.',
        'chinese': '首次打开时，无论通过按键还是系统菜单行，本菜单都会出现在屏幕中央，宽度足以完整显示菜单名称和页面。你拖动调整后的大小会被保留。',
    },
    # A11: a save that failed said "saved"
    'AMF_NotSavedSeeLog': {
        'english': 'not saved - see the log',
        'german': 'nicht gespeichert - siehe Log',
        'french': 'non enregistré - voir le journal',
        'spanish': 'no guardado: consulta el registro',
        'italian': 'non salvato: vedi il log',
        'polish': 'nie zapisano - zobacz log',
        'czech': 'neuloženo - viz log',
        'russian': 'не сохранено - см. журнал',
        'japanese': '保存できませんでした - ログを確認してください',
        'korean': '저장되지 않음 - 로그를 확인하세요',
        'chinese': '未保存 - 请查看日志',
    },
    # ---- B2: the wording pass. The twelve longest help texts said how the feature is built where the player needs what it
    # does; each is shorter and keeps every fact a player acts on. Nothing here is a format string.
    'AMF_FoldAllTip': {
        'english': 'Folds or unfolds every separator at once. One you fold or open by hand afterwards stays as you left it.',
        'german': 'Klappt alle Trenner auf einmal ein oder aus. Einer, den du danach von Hand ein- oder ausklappst, bleibt so.',
        'french': "Replie ou ouvre tous les séparateurs d'un coup. Celui que vous repliez ou ouvrez ensuite à la main reste comme vous l'avez laissé.",
        'spanish': 'Pliega o abre todos los separadores a la vez. El que pliegues o abras a mano después se queda como lo dejes.',
        'italian': 'Chiude o apre tutti i separatori in una volta. Quello che chiudi o apri a mano dopo resta come lo lasci.',
        'polish': 'Zwija lub rozwija wszystkie separatory naraz. Ten, który potem zwiniesz lub rozwiniesz ręcznie, zostaje tak, jak go zostawisz.',
        'czech': 'Sbalí nebo rozbalí všechny oddělovače najednou. Ten, který potom sbalíte nebo rozbalíte ručně, zůstane tak, jak ho necháte.',
        'russian': 'Сворачивает или разворачивает все разделители сразу. Тот, что вы потом свернёте или развернёте вручную, останется как есть.',
        'japanese': 'すべての区切りを一度に折りたたむ、または開きます。あとで手動で開閉した区切りは、そのままになります。',
        'korean': '모든 구분선을 한 번에 접거나 펼칩니다. 나중에 직접 접거나 펼친 구분선은 그대로 유지됩니다.',
        'chinese': '一次折叠或展开所有分隔符。之后手动折叠或展开的分隔符会保持你设定的状态。',
    },
    'AMF_TomlLoadHelp': {
        'english': 'Shows TOML settings files as pages, each named "<name> (TOML)"; Game Settings Override gets its own page. Every page starts off: turn on the ones you want below. A change is written into the file itself, and the file as it first was is kept as .amf-backup. A mod that can reload in game does so after each save; for the others the change applies the next time the game starts. Each page says which.',
        'german': 'Zeigt TOML-Einstellungsdateien als Seiten, jeweils "<Name> (TOML)"; Game Settings Override bekommt eine eigene Seite. Jede Seite ist zunächst aus: Schalte unten die gewünschten ein. Eine Änderung wird in die Datei selbst geschrieben, und die Datei in ihrem ersten Zustand bleibt als .amf-backup erhalten. Eine Mod, die im Spiel neu laden kann, tut das nach jedem Speichern; bei den anderen gilt die Änderung beim nächsten Spielstart. Jede Seite sagt, was für sie gilt.',
        'french': "Affiche les fichiers de réglages TOML sous forme de pages, nommées « <nom> (TOML) » ; Game Settings Override a sa propre page. Chaque page est d'abord désactivée : activez ci-dessous celles que vous voulez. Une modification est écrite dans le fichier lui-même, et le fichier tel qu'il était au départ est conservé en .amf-backup. Un mod capable de recharger en jeu le fait après chaque enregistrement ; pour les autres, la modification s'applique au prochain lancement du jeu. Chaque page indique son cas.",
        'spanish': 'Muestra los archivos de ajustes TOML como páginas, llamadas "<nombre> (TOML)"; Game Settings Override tiene su propia página. Cada página empieza desactivada: activa abajo las que quieras. Un cambio se escribe en el propio archivo, y el archivo tal como estaba al principio se conserva como .amf-backup. Un mod que puede recargar dentro del juego lo hace tras cada guardado; en los demás, el cambio se aplica la próxima vez que se inicie el juego. Cada página dice cuál es su caso.',
        'italian': 'Mostra i file di impostazioni TOML come pagine, chiamate "<nome> (TOML)"; Game Settings Override ha una pagina propria. Ogni pagina parte disattivata: attiva qui sotto quelle che vuoi. Una modifica viene scritta nel file stesso, e il file com\'era all\'inizio resta come .amf-backup. Una mod che può ricaricare in gioco lo fa dopo ogni salvataggio; per le altre la modifica vale al prossimo avvio del gioco. Ogni pagina dice quale caso è il suo.',
        'polish': 'Pokazuje pliki ustawień TOML jako strony o nazwie „<nazwa> (TOML)”; Game Settings Override ma własną stronę. Każda strona jest początkowo wyłączona: włącz poniżej te, których chcesz. Zmiana jest zapisywana w samym pliku, a plik w pierwotnej postaci zostaje jako .amf-backup. Mod, który potrafi przeładować się w grze, robi to po każdym zapisie; w pozostałych zmiana działa od następnego uruchomienia gry. Każda strona mówi, jak jest u niej.',
        'czech': 'Zobrazuje soubory nastavení TOML jako stránky s názvem „<název> (TOML)“; Game Settings Override má vlastní stránku. Každá stránka je nejprve vypnutá: zapněte níže ty, které chcete. Změna se zapíše přímo do souboru a soubor v původní podobě zůstane jako .amf-backup. Mod, který se umí ve hře znovu načíst, to udělá po každém uložení; u ostatních se změna projeví při příštím spuštění hry. Každá stránka uvádí, co pro ni platí.',
        'russian': 'Показывает файлы настроек TOML как страницы с именем «<имя> (TOML)»; у Game Settings Override своя страница. Каждая страница сначала выключена: включите ниже нужные. Изменение записывается в сам файл, а файл в исходном виде сохраняется как .amf-backup. Мод, который умеет перезагружаться в игре, делает это после каждого сохранения; для остальных изменение вступает в силу при следующем запуске игры. На каждой странице указано, как у неё.',
        'japanese': 'TOML設定ファイルを「<名前> (TOML)」というページとして表示します。Game Settings Override には専用のページがあります。どのページも最初はオフです。使いたいものを下でオンにしてください。変更はファイル自体に書き込まれ、最初の状態のファイルは .amf-backup として残ります。ゲーム中に再読み込みできるMODは保存のたびに再読み込みし、それ以外は次回のゲーム起動時に反映されます。どちらになるかは各ページに表示されます。',
        'korean': "TOML 설정 파일을 '<이름> (TOML)'이라는 페이지로 표시합니다. Game Settings Override는 전용 페이지를 가집니다. 모든 페이지는 처음에 꺼져 있으니, 원하는 것을 아래에서 켜세요. 변경 내용은 파일 자체에 기록되며, 처음 상태의 파일은 .amf-backup으로 보관됩니다. 게임 안에서 다시 불러올 수 있는 모드는 저장할 때마다 다시 불러오고, 나머지는 다음에 게임을 시작할 때 적용됩니다. 어느 쪽인지는 각 페이지에 표시됩니다.",
        'chinese': '将 TOML 设置文件显示为页面，名称为“<名称> (TOML)”；Game Settings Override 有自己的页面。每个页面初始为关闭：请在下方开启需要的页面。更改会写入文件本身，文件最初的样子会保留为 .amf-backup。能在游戏中重新加载的模组会在每次保存后重新加载；其余模组的更改在下次启动游戏时生效。每个页面都会说明属于哪种情况。',
    },
    'AMF_HoldHelp': {
        'english': "On, AMF opens this mod's menu for you, so the mod's own key is free for anything else. AMF writes a key no keyboard has into the mod's config file and keeps the file as it was as .amf-key-backup; turning this off puts the original key back. A change takes effect the next time the game starts.",
        'german': 'An: AMF öffnet das Menü dieser Mod für dich, sodass die eigene Taste der Mod für anderes frei ist. AMF schreibt eine Taste, die keine Tastatur hat, in die Konfigurationsdatei der Mod und bewahrt die Datei im alten Zustand als .amf-key-backup auf; Ausschalten stellt die ursprüngliche Taste wieder her. Eine Änderung gilt ab dem nächsten Spielstart.',
        'french': "Activé : AMF ouvre le menu de ce mod pour vous, et la touche du mod est libre pour autre chose. AMF écrit dans le fichier de configuration du mod une touche qu'aucun clavier ne possède et conserve le fichier tel qu'il était en .amf-key-backup ; désactiver remet la touche d'origine. Un changement prend effet au prochain lancement du jeu.",
        'spanish': 'Activado: AMF abre el menú de este mod por ti, así la tecla propia del mod queda libre para otra cosa. AMF escribe en el archivo de configuración del mod una tecla que ningún teclado tiene y conserva el archivo como estaba en .amf-key-backup; al desactivarlo vuelve la tecla original. Un cambio surte efecto la próxima vez que se inicie el juego.',
        'italian': 'Attivo: AMF apre per te il menu di questa mod, così il tasto della mod resta libero per altro. AMF scrive nel file di configurazione della mod un tasto che nessuna tastiera ha e conserva il file com\'era come .amf-key-backup; disattivando torna il tasto originale. Una modifica vale dal prossimo avvio del gioco.',
        'polish': 'Wł.: AMF otwiera menu tego moda za ciebie, więc własny klawisz moda jest wolny do czegoś innego. AMF zapisuje w pliku konfiguracyjnym moda klawisz, którego nie ma żadna klawiatura, i zachowuje plik w dawnej postaci jako .amf-key-backup; wyłączenie przywraca pierwotny klawisz. Zmiana działa od następnego uruchomienia gry.',
        'czech': 'Zap.: AMF otevírá nabídku tohoto modu za vás, takže vlastní klávesa modu je volná pro cokoli jiného. AMF zapíše do konfiguračního souboru modu klávesu, kterou žádná klávesnice nemá, a soubor v původní podobě uchová jako .amf-key-backup; vypnutím se vrátí původní klávesa. Změna se projeví při příštím spuštění hry.',
        'russian': 'Вкл.: AMF сам открывает меню этого мода, так что собственная клавиша мода свободна для другого. AMF записывает в файл настроек мода клавишу, которой нет ни на одной клавиатуре, и хранит файл в прежнем виде как .amf-key-backup; выключение возвращает исходную клавишу. Изменение вступает в силу при следующем запуске игры.',
        'japanese': 'オン：AMFがこのMODのメニューを代わりに開くので、MOD自身のキーをほかの用途に使えます。AMFはどのキーボードにもないキーをMODの設定ファイルに書き込み、元の状態のファイルを .amf-key-backup として保管します。オフにすると元のキーに戻ります。変更は次回のゲーム起動時に反映されます。',
        'korean': '켜기: AMF가 이 모드의 메뉴를 대신 열어 주므로, 모드 자체의 키를 다른 용도로 쓸 수 있습니다. AMF는 어떤 키보드에도 없는 키를 모드의 설정 파일에 기록하고, 원래 상태의 파일을 .amf-key-backup으로 보관합니다. 끄면 원래 키로 돌아갑니다. 변경은 다음에 게임을 시작할 때 적용됩니다.',
        'chinese': '开启：AMF 会替你打开这个模组的菜单，模组自己的按键就可以另作他用。AMF 会把一个任何键盘上都没有的按键写入模组的配置文件，并把原来的文件保留为 .amf-key-backup；关闭后会恢复原来的按键。更改在下次启动游戏时生效。',
    },
    'AMF_IniLoadHelp2': {
        'english': 'Shows INI settings files as pages, each named "<name> (INI)", unless the mod already has a page here. A change is written to the file itself, and the file as it first was is kept as .amf-backup. Mods and the game read these files when the game starts, so a change takes effect next time. Each page has its own switch below.',
        'german': 'Zeigt INI-Einstellungsdateien als Seiten, jeweils "<Name> (INI)", sofern die Mod hier nicht schon eine Seite hat. Eine Änderung wird in die Datei selbst geschrieben, und die Datei in ihrem ersten Zustand bleibt als .amf-backup erhalten. Mods und das Spiel lesen diese Dateien beim Spielstart, eine Änderung gilt also beim nächsten Mal. Jede Seite hat unten ihren eigenen Schalter.',
        'french': "Affiche les fichiers de réglages INI sous forme de pages, nommées « <nom> (INI) », sauf si le mod a déjà une page ici. Une modification est écrite dans le fichier lui-même, et le fichier tel qu'il était au départ est conservé en .amf-backup. Les mods et le jeu lisent ces fichiers au lancement du jeu : une modification prend donc effet la fois suivante. Chaque page a son propre interrupteur ci-dessous.",
        'spanish': 'Muestra los archivos de ajustes INI como páginas, llamadas "<nombre> (INI)", salvo que el mod ya tenga una página aquí. Un cambio se escribe en el propio archivo, y el archivo tal como estaba al principio se conserva como .amf-backup. Los mods y el juego leen estos archivos al iniciar el juego, así que un cambio surte efecto la próxima vez. Cada página tiene su propio interruptor abajo.',
        'italian': 'Mostra i file di impostazioni INI come pagine, chiamate "<nome> (INI)", a meno che la mod non abbia già una pagina qui. Una modifica viene scritta nel file stesso, e il file com\'era all\'inizio resta come .amf-backup. Le mod e il gioco leggono questi file all\'avvio del gioco, quindi una modifica vale dalla volta successiva. Ogni pagina ha il suo interruttore qui sotto.',
        'polish': 'Pokazuje pliki ustawień INI jako strony o nazwie „<nazwa> (INI)”, chyba że mod ma już tutaj stronę. Zmiana jest zapisywana w samym pliku, a plik w pierwotnej postaci zostaje jako .amf-backup. Mody i gra czytają te pliki przy starcie gry, więc zmiana działa od następnego razu. Każda strona ma poniżej własny przełącznik.',
        'czech': 'Zobrazuje soubory nastavení INI jako stránky s názvem „<název> (INI)“, pokud mod už nemá vlastní stránku. Změna se zapíše přímo do souboru a soubor v původní podobě zůstane jako .amf-backup. Mody i hra čtou tyto soubory při spuštění hry, takže se změna projeví příště. Každá stránka má níže vlastní přepínač.',
        'russian': 'Показывает файлы настроек INI как страницы с именем «<имя> (INI)», если у мода здесь ещё нет страницы. Изменение записывается в сам файл, а файл в исходном виде сохраняется как .amf-backup. Моды и игра читают эти файлы при запуске игры, поэтому изменение вступает в силу в следующий раз. У каждой страницы ниже свой переключатель.',
        'japanese': 'INI設定ファイルを「<名前> (INI)」というページとして表示します（そのMODのページがすでにある場合を除く）。変更はファイル自体に書き込まれ、最初の状態のファイルは .amf-backup として残ります。MODとゲームはゲーム起動時にこれらのファイルを読むため、変更は次回から反映されます。各ページのスイッチは下にあります。',
        'korean': "INI 설정 파일을 '<이름> (INI)'라는 페이지로 표시합니다(해당 모드의 페이지가 이미 있으면 제외). 변경 내용은 파일 자체에 기록되며, 처음 상태의 파일은 .amf-backup으로 보관됩니다. 모드와 게임은 게임을 시작할 때 이 파일을 읽으므로, 변경은 다음부터 적용됩니다. 각 페이지의 스위치는 아래에 있습니다.",
        'chinese': '将 INI 设置文件显示为页面，名称为“<名称> (INI)”，除非该模组在这里已有页面。更改会写入文件本身，文件最初的样子会保留为 .amf-backup。模组和游戏在启动游戏时读取这些文件，因此更改在下次启动时生效。每个页面在下方都有自己的开关。',
    },
    'AMF_SystemRowHelp2': {
        'english': "Adds a SKSE MENUS row to the journal's System tab, beside SAVE, LOAD and SETTINGS, which opens this menu. No game file is replaced, so it works with any menu artwork. Off: this menu is reached by its key alone.",
        'german': 'Fügt im System-Reiter des Tagebuchs neben SPEICHERN, LADEN und EINSTELLUNGEN eine Zeile SKSE-MENÜS ein, die dieses Menü öffnet. Es wird keine Spieldatei ersetzt, sie funktioniert also mit jeder Menügrafik. Aus: Dieses Menü ist nur über seine Taste erreichbar.',
        'french': "Ajoute une ligne MENUS SKSE à l'onglet Système du journal, à côté de SAUVEGARDER, CHARGER et PARAMÈTRES, qui ouvre ce menu. Aucun fichier du jeu n'est remplacé, donc cela fonctionne avec n'importe quelle interface. Désactivé : ce menu n'est accessible que par sa touche.",
        'spanish': 'Añade una fila MENÚS SKSE a la pestaña Sistema del diario, junto a GUARDAR, CARGAR y AJUSTES, que abre este menú. No se sustituye ningún archivo del juego, así que funciona con cualquier interfaz. Desactivado: este menú solo se abre con su tecla.',
        'italian': 'Aggiunge una riga MENU SKSE alla scheda Sistema del diario, accanto a SALVA, CARICA e IMPOSTAZIONI, che apre questo menu. Nessun file di gioco viene sostituito, quindi funziona con qualsiasi interfaccia. Disattivo: questo menu si raggiunge solo con il suo tasto.',
        'polish': 'Dodaje do zakładki System w dzienniku, obok ZAPISZ, WCZYTAJ i USTAWIENIA, wiersz MENU SKSE, który otwiera to menu. Żaden plik gry nie jest podmieniany, więc działa z każdą oprawą menu. Wył.: to menu otwiera się tylko swoim klawiszem.',
        'czech': 'Přidá do záložky Systém v deníku, vedle ULOŽIT, NAČÍST a NASTAVENÍ, řádek NABÍDKY SKSE, který otevře tuto nabídku. Žádný soubor hry se nenahrazuje, takže funguje s jakoukoli grafikou nabídek. Vyp.: tato nabídka se otevírá pouze svou klávesou.',
        'russian': 'Добавляет во вкладку «Система» дневника, рядом с СОХРАНИТЬ, ЗАГРУЗИТЬ и НАСТРОЙКИ, строку МЕНЮ SKSE, открывающую это меню. Файлы игры не заменяются, поэтому работает с любым оформлением меню. Выкл.: это меню открывается только своей клавишей.',
        'japanese': 'ジャーナルのシステムタブに、セーブ・ロード・設定の隣に「SKSEメニュー」の行を追加し、そこからこのメニューを開けます。ゲームファイルは置き換えないため、どのメニュー用アートワークでも動作します。オフ：このメニューはキーでのみ開きます。',
        'korean': "일지의 시스템 탭에서 저장, 불러오기, 설정 옆에 'SKSE 메뉴' 항목을 추가하며, 이 항목으로 이 메뉴를 엽니다. 게임 파일을 교체하지 않으므로 어떤 메뉴 아트워크와도 동작합니다. 끄기: 이 메뉴는 지정된 키로만 열 수 있습니다.",
        'chinese': '在日志的系统选项卡中，于保存、读取和设置旁边添加一行“SKSE 菜单”，用来打开本菜单。不替换任何游戏文件，因此可与任何菜单美术配合使用。关闭：本菜单仅通过其按键打开。',
    },
    'AMF_PauseGameHelp': {
        'english': "Stops game time while this menu is open, as the game's own menus do. Off: the game keeps running while you change settings. Another mod's window that asks to pause the game still stops time while it is open.",
        'german': 'Hält die Spielzeit an, solange dieses Menü offen ist, wie es die Menüs des Spiels tun. Aus: Das Spiel läuft weiter, während du Einstellungen änderst. Das Fenster einer anderen Mod, das eine Pause verlangt, hält die Zeit weiterhin an, solange es offen ist.',
        'french': "Arrête le temps du jeu tant que ce menu est ouvert, comme le font les menus du jeu. Désactivé : le jeu continue pendant que vous changez les réglages. La fenêtre d'un autre mod qui demande une pause arrête toujours le temps tant qu'elle est ouverte.",
        'spanish': 'Detiene el tiempo del juego mientras este menú está abierto, como hacen los menús del juego. Desactivado: el juego sigue en marcha mientras cambias ajustes. La ventana de otro mod que pida pausar el juego sigue deteniendo el tiempo mientras esté abierta.',
        'italian': 'Ferma il tempo di gioco mentre questo menu è aperto, come fanno i menu del gioco. Disattivo: il gioco continua mentre cambi le impostazioni. La finestra di un\'altra mod che chiede la pausa ferma comunque il tempo finché è aperta.',
        'polish': 'Zatrzymuje czas gry, gdy to menu jest otwarte, tak jak robią to menu samej gry. Wył.: gra toczy się dalej, gdy zmieniasz ustawienia. Okno innego moda, które prosi o pauzę, nadal zatrzymuje czas, dopóki jest otwarte.',
        'czech': 'Zastaví herní čas, dokud je tato nabídka otevřená, stejně jako vlastní nabídky hry. Vyp.: hra běží dál, zatímco měníte nastavení. Okno jiného modu, které žádá o pauzu, čas i nadále zastaví, dokud je otevřené.',
        'russian': 'Останавливает игровое время, пока это меню открыто, как это делают собственные меню игры. Выкл.: игра продолжается, пока вы меняете настройки. Окно другого мода, которое просит паузу, по-прежнему останавливает время, пока оно открыто.',
        'japanese': 'このメニューを開いている間、ゲーム自身のメニューと同じようにゲーム内の時間を止めます。オフ：設定を変えている間もゲームは動き続けます。ポーズを求める他のMODのウィンドウは、開いている間これまでどおり時間を止めます。',
        'korean': '이 메뉴가 열려 있는 동안 게임 자체 메뉴처럼 게임 시간을 멈춥니다. 끄기: 설정을 바꾸는 동안에도 게임이 계속 진행됩니다. 일시 정지를 요청하는 다른 모드의 창은 열려 있는 동안 여전히 시간을 멈춥니다.',
        'chinese': '本菜单打开时停止游戏时间，与游戏自带菜单一样。关闭：更改设置时游戏继续运行。其他模组要求暂停的窗口在打开期间仍会停止时间。',
    },
    'AMF_McmHideSkyUIHelp': {
        'english': "Takes a mod out of SkyUI's list when its whole menu is drawn here, so it is set in one place. A mod with anything this menu cannot draw stays in SkyUI's list. Off puts them all back.",
        'german': 'Nimmt eine Mod aus SkyUIs Liste, wenn ihr ganzes Menü hier gezeichnet wird, sodass sie an einem Ort eingestellt wird. Eine Mod mit etwas, das dieses Menü nicht zeichnen kann, bleibt in SkyUIs Liste. Aus stellt alle wieder zurück.',
        'french': "Retire un mod de la liste de SkyUI quand tout son menu est dessiné ici, pour le régler à un seul endroit. Un mod dont une partie ne peut pas être dessinée ici reste dans la liste de SkyUI. Désactivé les remet tous.",
        'spanish': 'Quita un mod de la lista de SkyUI cuando todo su menú se dibuja aquí, para ajustarlo en un solo sitio. Un mod con algo que este menú no puede dibujar se queda en la lista de SkyUI. Desactivado los devuelve todos.',
        'italian': 'Toglie una mod dalla lista di SkyUI quando tutto il suo menu è disegnato qui, così si imposta in un posto solo. Una mod con qualcosa che questo menu non sa disegnare resta nella lista di SkyUI. Disattivo le rimette tutte.',
        'polish': 'Usuwa moda z listy SkyUI, gdy całe jego menu jest rysowane tutaj, więc ustawia się go w jednym miejscu. Mod z czymś, czego to menu nie potrafi narysować, zostaje na liście SkyUI. Wył. przywraca wszystkie.',
        'czech': 'Odebere mod ze seznamu SkyUI, když se celé jeho menu vykresluje zde, takže se nastavuje na jednom místě. Mod s čímkoli, co toto menu neumí vykreslit, zůstane v seznamu SkyUI. Vypnutí je všechny vrátí.',
        'russian': 'Убирает мод из списка SkyUI, когда всё его меню отрисовывается здесь, чтобы он настраивался в одном месте. Мод, в котором есть то, что это меню не умеет рисовать, остаётся в списке SkyUI. Выключение возвращает все обратно.',
        'japanese': 'メニュー全体がここで描画されるMODをSkyUIのリストから外し、1か所で設定できるようにします。このメニューで描画できないものを含むMODはSkyUIのリストに残ります。オフにするとすべて元に戻ります。',
        'korean': '메뉴 전체가 여기에 그려지는 모드를 SkyUI 목록에서 빼서 한곳에서 설정하도록 합니다. 이 메뉴가 그릴 수 없는 것이 있는 모드는 SkyUI 목록에 남습니다. 끄면 모두 되돌립니다.',
        'chinese': '当模组的整个菜单都能在这里绘制时，把它从 SkyUI 的列表中移出，这样只需在一处设置。含有本菜单无法绘制内容的模组会留在 SkyUI 的列表中。关闭后全部放回。',
    },
    'AMF_McmImportHelp': {
        'english': "Switch a menu off to keep it in SkyUI's menu only. It is then never taken out of SkyUI's list.",
        'german': 'Schalte ein Menü aus, um es nur in SkyUIs Menü zu behalten. Es wird dann nie aus SkyUIs Liste genommen.',
        'french': "Désactivez un menu pour le garder uniquement dans le menu de SkyUI. Il n'est alors jamais retiré de la liste de SkyUI.",
        'spanish': 'Desactiva un menú para dejarlo solo en el menú de SkyUI. Entonces nunca se quita de la lista de SkyUI.',
        'italian': 'Disattiva un menu per tenerlo solo nel menu di SkyUI. In quel caso non viene mai tolto dalla lista di SkyUI.',
        'polish': 'Wyłącz menu, aby zostało tylko w menu SkyUI. Nie jest wtedy nigdy usuwane z listy SkyUI.',
        'czech': 'Vypněte menu, aby zůstalo jen v menu SkyUI. Ze seznamu SkyUI se pak nikdy neodebere.',
        'russian': 'Выключите меню, чтобы оставить его только в меню SkyUI. Тогда оно никогда не убирается из списка SkyUI.',
        'japanese': 'メニューをオフにすると、SkyUIのメニューだけに残ります。その場合、SkyUIのリストから外されることはありません。',
        'korean': '메뉴를 끄면 SkyUI 메뉴에만 남습니다. 그러면 SkyUI 목록에서 빠지는 일이 없습니다.',
        'chinese': '关闭某个菜单即只保留在 SkyUI 的菜单中。这样它就不会被移出 SkyUI 的列表。',
    },
    'AMF_McmMemButtonsHelp': {
        'english': "Back up all now saves the current values of every MCM menu that can be read, including what you set in SkyUI's own menu. Restore now sets this game's menus to the profile.",
        'german': 'Jetzt alles sichern speichert die aktuellen Werte jedes MCM-Menüs, das gelesen werden kann - auch was du in SkyUIs eigenem Menü eingestellt hast. Jetzt wiederherstellen setzt die Menüs dieses Spiels auf das Profil.',
        'french': "Tout sauvegarder maintenant enregistre les valeurs actuelles de chaque menu MCM lisible, y compris ce que vous avez réglé dans le menu de SkyUI. Restaurer maintenant règle les menus de cette partie sur le profil.",
        'spanish': 'Guardar todo ahora guarda los valores actuales de cada menú MCM que se puede leer, también lo que ajustaste en el menú propio de SkyUI. Restaurar ahora pone los menús de esta partida según el perfil.',
        'italian': 'Salva tutto ora salva i valori attuali di ogni menu MCM leggibile, anche ciò che hai impostato nel menu di SkyUI. Ripristina ora imposta i menu di questa partita sul profilo.',
        'polish': 'Zapisz wszystko teraz zapisuje bieżące wartości każdego menu MCM, które da się odczytać - także to, co ustawiono we własnym menu SkyUI. Przywróć teraz ustawia menu tej gry według profilu.',
        'czech': 'Zálohovat vše teď uloží aktuální hodnoty každého menu MCM, které lze přečíst - i to, co jste nastavili ve vlastním menu SkyUI. Obnovit teď nastaví menu této hry podle profilu.',
        'russian': 'Сохранить всё сейчас сохраняет текущие значения каждого меню MCM, которое можно прочитать, - и то, что вы настроили в собственном меню SkyUI. Восстановить сейчас ставит меню этой игры по профилю.',
        'japanese': '「今すぐすべてバックアップ」は、読み取れるすべてのMCMメニューの現在の値を保存します（SkyUI 自身のメニューで設定した内容も含む）。「今すぐ復元」はこのゲームのメニューをプロファイルの値に設定します。',
        'korean': '지금 모두 백업은 읽을 수 있는 모든 MCM 메뉴의 현재 값을 저장합니다 - SkyUI 자체 메뉴에서 설정한 내용도 포함됩니다. 지금 복원은 이 게임의 메뉴를 프로필 값으로 설정합니다.',
        'chinese': '“立即全部备份”会保存所有可读取的 MCM 菜单的当前值，包括你在 SkyUI 自己的菜单里设置的内容。“立即恢复”会把本局游戏的菜单设为配置档中的值。',
    },
    'AMF_KeepCameraAwakeHelp': {
        'english': 'Stops the idle camera circling your character while this menu is open with the game running behind it. With the game paused by this menu it changes nothing.',
        'german': 'Verhindert, dass die Leerlaufkamera um deinen Charakter kreist, solange dieses Menü offen ist und das Spiel dahinter läuft. Ist das Spiel durch dieses Menü pausiert, ändert es nichts.',
        'french': "Empêche la caméra d'inactivité de tourner autour de votre personnage tant que ce menu est ouvert et que le jeu tourne derrière. Si le jeu est mis en pause par ce menu, cela ne change rien.",
        'spanish': 'Evita que la cámara de inactividad gire alrededor de tu personaje mientras este menú está abierto y el juego sigue en marcha detrás. Con el juego pausado por este menú no cambia nada.',
        'italian': 'Impedisce alla telecamera di inattività di girare attorno al tuo personaggio mentre questo menu è aperto e il gioco continua dietro. Con il gioco in pausa per questo menu non cambia nulla.',
        'polish': 'Powstrzymuje kamerę bezczynności przed krążeniem wokół postaci, gdy to menu jest otwarte, a gra toczy się w tle. Gdy gra jest wstrzymana przez to menu, niczego to nie zmienia.',
        'czech': 'Zabrání nečinné kameře kroužit kolem vaší postavy, dokud je tato nabídka otevřená a hra za ní běží. Když je hra touto nabídkou pozastavená, nic se nemění.',
        'russian': 'Не даёт камере бездействия кружить вокруг персонажа, пока это меню открыто, а игра за ним идёт. Если игра поставлена на паузу этим меню, ничего не меняется.',
        'japanese': 'このメニューを開いたままゲームが裏で動いている間、待機カメラがキャラクターの周りを回り始めないようにします。このメニューでゲームが一時停止しているときは何も変わりません。',
        'korean': '이 메뉴가 열려 있고 게임이 뒤에서 진행되는 동안, 대기 카메라가 캐릭터 주위를 돌지 않게 합니다. 이 메뉴로 게임이 일시 정지된 상태에서는 아무것도 바뀌지 않습니다.',
        'chinese': '本菜单打开且游戏在后面继续运行时，阻止待机镜头围绕角色旋转。游戏被本菜单暂停时不起作用。',
    },
    'AMF_CleanStateHelp': {
        'english': 'Each time the game starts, deletes the .amf-state.ini files of saves you have deleted. A file whose save is still there is never touched, and each deleted file is named in the log.',
        'german': 'Löscht bei jedem Spielstart die .amf-state.ini-Dateien von Spielständen, die du gelöscht hast. Eine Datei, deren Spielstand noch da ist, wird nie angerührt, und jede gelöschte Datei wird im Log genannt.',
        'french': "À chaque lancement du jeu, supprime les fichiers .amf-state.ini des sauvegardes que vous avez supprimées. Un fichier dont la sauvegarde existe encore n'est jamais touché, et chaque fichier supprimé est nommé dans le journal.",
        'spanish': 'Cada vez que se inicia el juego, borra los archivos .amf-state.ini de las partidas guardadas que has borrado. Un archivo cuya partida sigue ahí nunca se toca, y cada archivo borrado se nombra en el registro.',
        'italian': "A ogni avvio del gioco elimina i file .amf-state.ini dei salvataggi che hai eliminato. Un file il cui salvataggio c'è ancora non viene mai toccato, e ogni file eliminato è indicato nel log.",
        'polish': 'Przy każdym uruchomieniu gry usuwa pliki .amf-state.ini zapisów, które usunąłeś. Plik, którego zapis nadal istnieje, nigdy nie jest ruszany, a każdy usunięty plik jest wymieniony w logu.',
        'czech': 'Při každém spuštění hry smaže soubory .amf-state.ini uložených pozic, které jste smazali. Soubor, jehož uložená pozice stále existuje, se nikdy nedotkne, a každý smazaný soubor je uveden v logu.',
        'russian': 'При каждом запуске игры удаляет файлы .amf-state.ini тех сохранений, которые вы удалили. Файл, сохранение которого ещё на месте, никогда не трогается, а каждый удалённый файл указывается в журнале.',
        'japanese': 'ゲームを起動するたびに、削除したセーブの .amf-state.ini ファイルを削除します。セーブが残っているファイルには手を付けず、削除したファイルはすべてログに記録されます。',
        'korean': '게임을 시작할 때마다, 삭제한 세이브의 .amf-state.ini 파일을 삭제합니다. 세이브가 남아 있는 파일은 건드리지 않으며, 삭제한 파일은 모두 로그에 기록됩니다.',
        'chinese': '每次启动游戏时，删除已被你删除的存档所对应的 .amf-state.ini 文件。存档仍在的文件绝不会被改动，每个被删除的文件都会记入日志。',
    },
    'AMF_LayoutPresetsHelp': {
        'english': 'Save this list - its order, separators, favourites and names - under a name, and load it back any time.',
        'german': 'Speichere diese Liste - Reihenfolge, Trenner, Favoriten und Namen - unter einem Namen und lade sie jederzeit wieder.',
        'french': 'Enregistrez cette liste - son ordre, ses séparateurs, ses favoris et ses noms - sous un nom, et rechargez-la à tout moment.',
        'spanish': 'Guarda esta lista - su orden, separadores, favoritos y nombres - con un nombre y cárgala cuando quieras.',
        'italian': 'Salva questo elenco - ordine, separatori, preferiti e nomi - con un nome e ricaricalo quando vuoi.',
        'polish': 'Zapisz tę listę - kolejność, separatory, ulubione i nazwy - pod nazwą i wczytaj ją w dowolnej chwili.',
        'czech': 'Uložte tento seznam - pořadí, oddělovače, oblíbené a názvy - pod jménem a kdykoli ho znovu načtěte.',
        'russian': 'Сохраните этот список - порядок, разделители, избранное и названия - под именем и загружайте его в любой момент.',
        'japanese': 'このリスト（並び順・区切り・お気に入り・名前）を名前を付けて保存し、いつでも読み込めます。',
        'korean': '이 목록(순서, 구분선, 즐겨찾기, 이름)을 이름을 붙여 저장하고 언제든 다시 불러올 수 있습니다.',
        'chinese': '将此列表（顺序、分隔符、收藏和名称）以一个名称保存，随时可以重新载入。',
    },
    # B21: a switch's state, where a mod's help text shows it
    'AMF_On': {
        'english': 'on', 'german': 'an', 'french': 'activé', 'spanish': 'activado', 'italian': 'attivo', 'polish': 'wł.',
        'czech': 'zapnuto', 'russian': 'вкл.', 'japanese': 'オン', 'korean': '켜짐', 'chinese': '开启',
    },
    'AMF_Off': {
        'english': 'off', 'german': 'aus', 'french': 'désactivé', 'spanish': 'desactivado', 'italian': 'disattivo', 'polish': 'wył.',
        'czech': 'vypnuto', 'russian': 'выкл.', 'japanese': 'オフ', 'korean': '꺼짐', 'chinese': '关闭',
    },
    # C5: the source endings on converted menus' names can be switched off
    'AMF_ShowSourceTags': {
        'english': 'Show where a converted menu comes from: (MCM), (INI), (FLICK)',
        'german': 'Zeigen, woher ein umgewandeltes Menü stammt: (MCM), (INI), (FLICK)',
        'french': "Afficher d'où vient un menu converti : (MCM), (INI), (FLICK)",
        'spanish': 'Mostrar de dónde viene un menú convertido: (MCM), (INI), (FLICK)',
        'italian': 'Mostra da dove viene un menu convertito: (MCM), (INI), (FLICK)',
        'polish': 'Pokazuj, skąd pochodzi przekonwertowane menu: (MCM), (INI), (FLICK)',
        'czech': 'Zobrazovat, odkud převedená nabídka pochází: (MCM), (INI), (FLICK)',
        'russian': 'Показывать, откуда взято преобразованное меню: (MCM), (INI), (FLICK)',
        'japanese': '変換されたメニューの出どころを表示する: (MCM)、(INI)、(FLICK)',
        'korean': '변환된 메뉴의 출처 표시: (MCM), (INI), (FLICK)',
        'chinese': '显示转换菜单的来源：(MCM)、(INI)、(FLICK)',
    },
    'AMF_ShowSourceTagsHelp': {
        'english': "Off: the list and the page title show the mod's name alone. A mod with its own page and a converted one then shows twice under one name.",
        'german': 'Aus: Liste und Seitentitel zeigen nur den Namen der Mod. Eine Mod mit eigener Seite und einer umgewandelten erscheint dann zweimal unter demselben Namen.',
        'french': "Désactivé : la liste et le titre de la page n'affichent que le nom du mod. Un mod qui a sa propre page et une page convertie apparaît alors deux fois sous le même nom.",
        'spanish': 'Desactivado: la lista y el título de la página muestran solo el nombre del mod. Un mod con página propia y otra convertida aparece entonces dos veces con el mismo nombre.',
        'italian': 'Disattivo: l\'elenco e il titolo della pagina mostrano solo il nome della mod. Una mod con una pagina propria e una convertita compare allora due volte con lo stesso nome.',
        'polish': 'Wyłączone: lista i tytuł strony pokazują samą nazwę moda. Mod z własną stroną i stroną przekonwertowaną pojawia się wtedy dwa razy pod tą samą nazwą.',
        'czech': 'Vypnuto: seznam a titulek stránky ukazují jen název modu. Mod s vlastní stránkou i převedenou se pak zobrazí dvakrát pod stejným názvem.',
        'russian': 'Выкл.: в списке и в заголовке страницы показывается только имя мода. Мод с собственной страницей и преобразованной тогда отображается дважды под одним именем.',
        'japanese': 'オフ：一覧とページのタイトルにはMODの名前だけが表示されます。独自のページと変換されたページの両方を持つMODは、同じ名前で2つ表示されます。',
        'korean': '끄기: 목록과 페이지 제목에 모드 이름만 표시됩니다. 자체 페이지와 변환된 페이지를 모두 가진 모드는 같은 이름으로 두 번 표시됩니다.',
        'chinese': '关闭：列表和页面标题只显示模组名称。同时拥有自带页面和转换页面的模组会以同一名称显示两次。',
    },
    # B12: the key row's prompt while it waits says how to cancel (Escape, or the pad's close button)
    'AMF_McmPressKey': {
        'english': 'Press a key or button (Esc cancels)',
        'german': 'Taste oder Knopf drücken (Esc bricht ab)',
        'french': 'Appuyez sur une touche ou un bouton (Échap annule)',
        'spanish': 'Pulsa una tecla o un botón (Esc cancela)',
        'italian': 'Premi un tasto o un pulsante (Esc annulla)',
        'polish': 'Naciśnij klawisz lub przycisk (Esc anuluje)',
        'czech': 'Stiskněte klávesu nebo tlačítko (Esc ruší)',
        'russian': 'Нажмите клавишу или кнопку (Esc - отмена)',
        'japanese': 'キーかボタンを押してください (Escでキャンセル)',
        'korean': '키나 버튼을 누르세요 (Esc: 취소)',
        'chinese': '请按下按键或按钮（Esc 取消）',
    },
    # C3: Settings > General's two sub-tabs
    'AMF_SubGeneralMenu': {
        'english': 'Menu', 'german': 'Menü', 'french': 'Menu', 'spanish': 'Menú', 'italian': 'Menu', 'polish': 'Menu',
        'czech': 'Nabídka', 'russian': 'Меню', 'japanese': 'メニュー', 'korean': '메뉴', 'chinese': '菜单',
    },
    'AMF_SubGeneralKeyboard': {
        'english': 'On-screen keyboard', 'german': 'Bildschirmtastatur', 'french': "Clavier à l'écran",
        'spanish': 'Teclado en pantalla', 'italian': 'Tastiera su schermo', 'polish': 'Klawiatura ekranowa',
        'czech': 'Klávesnice na obrazovce', 'russian': 'Экранная клавиатура', 'japanese': 'スクリーンキーボード',
        'korean': '화면 키보드', 'chinese': '屏幕键盘',
    },
    # B8: a button that destroys something asks for a second press
    'AMF_ConfirmAgain': {
        'english': 'Press again to confirm',
        'german': 'Zum Bestätigen erneut drücken',
        'french': 'Appuyez de nouveau pour confirmer',
        'spanish': 'Pulsa otra vez para confirmar',
        'italian': 'Premi di nuovo per confermare',
        'polish': 'Naciśnij ponownie, aby potwierdzić',
        'czech': 'Stiskněte znovu pro potvrzení',
        'russian': 'Нажмите ещё раз для подтверждения',
        'japanese': 'もう一度押して確定',
        'korean': '다시 눌러 확인',
        'chinese': '再按一次以确认',
    },
    # A10: Korean takes a particle after the key's name that depends on how the name ends; "키" (key) after it keeps it right
    'AMF_ManOpening1': {
        'korean': '%s 키를 눌러 메뉴를 열고 다시 %s 키를 눌러 닫습니다. Escape로도 닫힙니다. 키는 바꿀 수 있습니다: 조작 -> 메뉴 열고 닫기 -> 다시 지정을 누른 뒤 원하는 키를 누르세요.',
    },
    'AMF_ManTrouble2': {
        'korean': '메뉴가 열리지 않음: 다른 무언가가 %s 키를 쓰고 있을 수 있습니다. 조작에서 다시 지정하거나, 일지의 시스템 탭에서 메뉴를 여세요.',
    },
}

# key -> list of (name to trade for "%s", how many times it must be found)
SWAP = {
    'AMF_OskHint': [('X', 1), ('Y', 1)],
    'AMF_OskHintGame': [('X', 1), ('Y', 1)],
    'AMF_OskHintMore': [('RT', 1)],
    'AMF_ManOpening1': [('F1', 2)],
    'AMF_ManTrouble2': [('F1', 1)],
}

REMOVE = [
    'AMF_ManTrouble5',   # A10: advice for versions before 1.9.5, shown to people on 2.4.x
]


def swap(key, value, lang):
    if '%s' in value:
        return value
    out = value
    for name, count in SWAP[key]:
        found = out.count(name)
        if found != count:
            raise SystemExit('%s: %s holds "%s" %d time(s), expected %d - translate it by hand in SET' % (lang, key, name, found, count))
    # left to right, so the order of the "%s" follows the order of the names in the sentence
    marks = []
    for name, _ in SWAP[key]:
        start = 0
        while True:
            at = out.find(name, start)
            if at < 0:
                break
            marks.append((at, len(name)))
            start = at + len(name)
    for at, length in sorted(marks, reverse=True):
        out = out[:at] + '%s' + out[at + length:]
    return out


def main():
    check = '--check' in sys.argv
    dirty = 0
    for lang in LANGS:
        path = os.path.join(DIR, 'ApocryphaMenuFramework_%s.txt' % lang)
        text = io.open(path, encoding='utf-16', newline='').read()
        lines = text.split('\r\n')
        tail_blank = lines and lines[-1] == ''
        if tail_blank:
            lines.pop()
        index = {}
        for i, line in enumerate(lines):
            if line.startswith('$') and '\t' in line:
                index[line.split('\t', 1)[0][1:]] = i
        changed = []
        for key in SWAP:
            if key in SET and lang in SET[key]:
                continue   # written out by hand for this language
            if key not in index:
                raise SystemExit('%s: %s is missing' % (lang, key))
            old = lines[index[key]].split('\t', 1)[1]
            new = swap(key, old, lang)
            if new != old:
                lines[index[key]] = '$%s\t%s' % (key, new)
                changed.append(key)
        for key, per_lang in SET.items():
            if lang not in per_lang:
                continue
            row = '$%s\t%s' % (key, per_lang[lang])
            if key in index:
                if lines[index[key]] != row:
                    lines[index[key]] = row
                    changed.append(key)
            else:
                lines.append(row)
                index[key] = len(lines) - 1
                changed.append(key)
        for key in REMOVE:
            if key in index:
                lines[index[key]] = None
                changed.append('-' + key)
        lines = [line for line in lines if line is not None]
        if changed:
            dirty += 1
            print('%-9s %s' % (lang, ', '.join(changed)))
            if not check:
                out = '\r\n'.join(lines) + ('\r\n' if tail_blank else '')
                with io.open(path, 'w', encoding='utf-16-le', newline='') as handle:
                    handle.write('﻿' + out)
    print('%d file(s) %s' % (dirty, 'would change' if check else 'changed'))
    return 1 if (check and dirty) else 0


if __name__ == '__main__':
    sys.exit(main())
