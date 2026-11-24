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
