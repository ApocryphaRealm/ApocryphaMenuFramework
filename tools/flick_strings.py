# The FLICK API 6/7 host's strings (2.3.0, 4. plans\amf-flick-v6\PLAN.md), in every language AMF ships. Run once to append
# any key a translation file lacks: python -I tools\flick_strings.py   (--check: report only). Files stay UTF-16 LE with a
# BOM and CRLF, as SkyUI's and SKSE's translation files are. Every %s stays in the same order as the English.
import os
import re
import sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist', 'Interface', 'Translations')

S = {
    'english': {
        'AMF_FlickTooOld': 'Kept in this menu: it needs FLICK API %s, and the installed FLICK %s is older. Its windows need a newer FLICK.',
        'AMF_FlickCamera': "It moves the game camera through FLICK, which only FLICK itself does, so it goes to FLICK's own window unless you choose this menu.",
        'AMF_FlickCameraNoFlick': 'It moves the game camera through FLICK, which only FLICK itself does. FLICK is not installed, so its camera controls do nothing.',
        'AMF_FlickCameraNeedsFlick': 'The camera controls on this page need FLICK itself; this menu does not move the game camera.',
    },
    'german': {
        'AMF_FlickTooOld': 'In diesem Menü behalten: Sie braucht FLICK-API %s, und das installierte FLICK %s ist älter. Ihre Fenster brauchen ein neueres FLICK.',
        'AMF_FlickCamera': 'Diese Mod bewegt die Spielkamera über FLICK, was nur FLICK selbst kann, daher kommt sie in FLICKs eigenes Fenster, sofern du nicht dieses Menü wählst.',
        'AMF_FlickCameraNoFlick': 'Diese Mod bewegt die Spielkamera über FLICK, was nur FLICK selbst kann. FLICK ist nicht installiert, daher tun ihre Kamerasteuerungen nichts.',
        'AMF_FlickCameraNeedsFlick': 'Die Kamerasteuerung auf dieser Seite braucht FLICK selbst; dieses Menü bewegt die Spielkamera nicht.',
    },
    'french': {
        'AMF_FlickTooOld': "Gardé dans ce menu : il lui faut l'API FLICK %s, et le FLICK %s installé est plus ancien. Ses fenêtres demandent un FLICK plus récent.",
        'AMF_FlickCamera': "Ce mod déplace la caméra du jeu via FLICK, ce que seul FLICK lui-même fait ; il va donc dans la fenêtre de FLICK, sauf si vous choisissez ce menu.",
        'AMF_FlickCameraNoFlick': "Ce mod déplace la caméra du jeu via FLICK, ce que seul FLICK lui-même fait. FLICK n'est pas installé, ses commandes de caméra ne font donc rien.",
        'AMF_FlickCameraNeedsFlick': 'Les commandes de caméra de cette page demandent FLICK lui-même ; ce menu ne déplace pas la caméra du jeu.',
    },
    'spanish': {
        'AMF_FlickTooOld': 'Mantenido en este menú: necesita la API %s de FLICK, y el FLICK %s instalado es más antiguo. Sus ventanas necesitan un FLICK más reciente.',
        'AMF_FlickCamera': 'Este mod mueve la cámara del juego a través de FLICK, algo que solo hace FLICK, así que va a la ventana de FLICK salvo que elijas este menú.',
        'AMF_FlickCameraNoFlick': 'Este mod mueve la cámara del juego a través de FLICK, algo que solo hace FLICK. FLICK no está instalado, así que sus controles de cámara no hacen nada.',
        'AMF_FlickCameraNeedsFlick': 'Los controles de cámara de esta página necesitan el propio FLICK; este menú no mueve la cámara del juego.',
    },
    'italian': {
        'AMF_FlickTooOld': "Tenuta in questo menu: richiede l'API %s di FLICK, e il FLICK %s installato è più vecchio. Le sue finestre richiedono un FLICK più recente.",
        'AMF_FlickCamera': 'Questa mod muove la telecamera del gioco tramite FLICK, cosa che fa solo FLICK, quindi va nella finestra di FLICK a meno che tu non scelga questo menu.',
        'AMF_FlickCameraNoFlick': 'Questa mod muove la telecamera del gioco tramite FLICK, cosa che fa solo FLICK. FLICK non è installato, quindi i suoi comandi della telecamera non fanno nulla.',
        'AMF_FlickCameraNeedsFlick': 'I comandi della telecamera di questa pagina richiedono FLICK stesso; questo menu non muove la telecamera del gioco.',
    },
    'polish': {
        'AMF_FlickTooOld': 'Zostawiony w tym menu: potrzebuje API FLICK %s, a zainstalowany FLICK %s jest starszy. Jego okna wymagają nowszego FLICK.',
        'AMF_FlickCamera': 'Ten mod porusza kamerą gry przez FLICK, co robi tylko sam FLICK, więc trafia do własnego okna FLICK, chyba że wybierzesz to menu.',
        'AMF_FlickCameraNoFlick': 'Ten mod porusza kamerą gry przez FLICK, co robi tylko sam FLICK. FLICK nie jest zainstalowany, więc jego sterowanie kamerą nic nie robi.',
        'AMF_FlickCameraNeedsFlick': 'Sterowanie kamerą na tej stronie wymaga samego FLICK; to menu nie porusza kamerą gry.',
    },
    'czech': {
        'AMF_FlickTooOld': 'Ponecháno v tomto menu: potřebuje FLICK API %s a nainstalovaný FLICK %s je starší. Jeho okna potřebují novější FLICK.',
        'AMF_FlickCamera': 'Tento mód pohybuje herní kamerou přes FLICK, což umí jen samotný FLICK, takže jde do vlastního okna FLICK, pokud nezvolíte toto menu.',
        'AMF_FlickCameraNoFlick': 'Tento mód pohybuje herní kamerou přes FLICK, což umí jen samotný FLICK. FLICK není nainstalován, takže jeho ovládání kamery nic nedělá.',
        'AMF_FlickCameraNeedsFlick': 'Ovládání kamery na této stránce potřebuje samotný FLICK; toto menu herní kamerou nepohybuje.',
    },
    'russian': {
        'AMF_FlickTooOld': 'Оставлен в этом меню: ему нужен API FLICK %s, а установленный FLICK %s старше. Его окнам нужен более новый FLICK.',
        'AMF_FlickCamera': 'Этот мод двигает игровую камеру через FLICK, что умеет только сам FLICK, поэтому он уходит в собственное окно FLICK, если вы не выберете это меню.',
        'AMF_FlickCameraNoFlick': 'Этот мод двигает игровую камеру через FLICK, что умеет только сам FLICK. FLICK не установлен, поэтому его управление камерой ничего не делает.',
        'AMF_FlickCameraNeedsFlick': 'Управлению камерой на этой странице нужен сам FLICK; это меню не двигает игровую камеру.',
    },
    'japanese': {
        'AMF_FlickTooOld': 'このメニューに置いています：FLICK API %s が必要ですが、インストールされているFLICK %s はそれより古いです。ウィンドウにはより新しいFLICKが必要です。',
        'AMF_FlickCamera': 'このModはFLICKを通してゲームのカメラを動かします。これができるのはFLICK本体だけなので、このメニューを選ばない限りFLICK自身のウィンドウに入ります。',
        'AMF_FlickCameraNoFlick': 'このModはFLICKを通してゲームのカメラを動かします。これができるのはFLICK本体だけです。FLICKがインストールされていないため、カメラ操作は何もしません。',
        'AMF_FlickCameraNeedsFlick': 'このページのカメラ操作にはFLICK本体が必要です。このメニューはゲームのカメラを動かしません。',
    },
    'korean': {
        'AMF_FlickTooOld': '이 메뉴에 둡니다: FLICK API %s이(가) 필요한데 설치된 FLICK %s은(는) 그보다 오래되었습니다. 창을 보려면 더 새로운 FLICK이 필요합니다.',
        'AMF_FlickCamera': '이 모드는 FLICK을 통해 게임 카메라를 움직이며, 이는 FLICK 자체만 할 수 있습니다. 따라서 이 메뉴를 선택하지 않는 한 FLICK 자체 창으로 갑니다.',
        'AMF_FlickCameraNoFlick': '이 모드는 FLICK을 통해 게임 카메라를 움직이며, 이는 FLICK 자체만 할 수 있습니다. FLICK이 설치되어 있지 않아 카메라 조작은 아무것도 하지 않습니다.',
        'AMF_FlickCameraNeedsFlick': '이 페이지의 카메라 조작에는 FLICK 자체가 필요합니다. 이 메뉴는 게임 카메라를 움직이지 않습니다.',
    },
    'chinese': {
        'AMF_FlickTooOld': '保留在本菜单：它需要 FLICK API %s，而已安装的 FLICK %s 较旧。它的窗口需要更新的 FLICK。',
        'AMF_FlickCamera': '此模组通过 FLICK 移动游戏镜头，只有 FLICK 本身能做到，因此除非你选择本菜单，它会进入 FLICK 自己的窗口。',
        'AMF_FlickCameraNoFlick': '此模组通过 FLICK 移动游戏镜头，只有 FLICK 本身能做到。未安装 FLICK，因此它的镜头控制不起作用。',
        'AMF_FlickCameraNeedsFlick': '此页面上的镜头控制需要 FLICK 本身；本菜单不会移动游戏镜头。',
    },
}

SPEC = re.compile(r'%[sd]')


def main():
    check = '--check' in sys.argv
    keys = list(S['english'])
    bad = 0
    for lang, table in S.items():
        missing_here = [k for k in keys if k not in table]
        if missing_here:
            print(f'{lang}: no text for {missing_here}')
            bad += 1
        for k in keys:
            if k in table and SPEC.findall(table[k]) != SPEC.findall(S['english'][k]):
                print(f'{lang} {k}: %s / %d order differs from English')
                bad += 1
        path = os.path.join(DIR, f'ApocryphaMenuFramework_{lang}.txt')
        raw = open(path, 'rb').read()
        text = raw.decode('utf-16')
        have = {l.split('\t', 1)[0] for l in text.splitlines() if '\t' in l}
        add = [k for k in keys if '$' + k not in have and k in table]
        if check or not add or bad:
            print(f'{lang}: {len(add)} to add')
            continue
        tail = '' if text.endswith('\r\n') or text.endswith('\n') else '\r\n'
        text += tail + ''.join(f'${k}\t{table[k]}\r\n' for k in add)
        open(path, 'wb').write(b'\xff\xfe' + text.lstrip('﻿').encode('utf-16-le'))
        print(f'{lang}: {len(add)} added')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
