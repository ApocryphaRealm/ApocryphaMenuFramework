# The remembered settings' strings (2.1.2, the owner 2026-10-06), in every language AMF ships. Run once to append any key
# a translation file lacks: python -I tools\mcm_memory_strings.py   (--check: report only). Files stay UTF-16 LE with a
# BOM and CRLF, as SkyUI's and SKSE's translation files are. "MCM" is written out in body text in each language (2.1.2);
# MCM Helper, a product name, stays.
import os
import sys

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dist', 'Interface', 'Translations')

S = {
    'english': {
        'AMF_McmMemTitle': 'Remembered settings',
        'AMF_McmMemHelp': "A new game forgets what menus written in a mod's script, and MCM Helper settings kept in your save, were set to. Those settings are remembered here and set again after a new game. MCM Helper's other settings are kept by MCM Helper itself.",
        'AMF_McmMemAuto': 'Remember each change made here',
        'AMF_McmMemOnNewGame': 'Set them again after a new game',
        'AMF_McmMemProfile': 'Profile',
        'AMF_McmMemNewName': 'New profile name',
        'AMF_McmMemNew': 'New',
        'AMF_McmMemCopy': 'Copy this one',
        'AMF_McmMemExists': 'A profile with that name exists already.',
        'AMF_McmMemDelete': 'Delete a profile...',
        'AMF_McmMemBackUp': 'Back up all now',
        'AMF_McmMemRestore': 'Restore now',
        'AMF_McmMemWait': 'Close the MCM page that is open, or wait for the backup or restore that is running.',
        'AMF_McmMemButtonsHelp': "Back up all now reads every menu this menu can read - also what you set in SkyUI's own menu. Restore now sets this game's menus to the profile.",
        'AMF_McmMemMenus': 'Menus in this profile (%d)',
        'AMF_McmMemMenusHelp': 'Switch off a menu to leave it out of the automatic restore after a new game; Forget drops what this profile remembers of it.',
        'AMF_McmMemSaved': '%d saved',
        'AMF_McmMemAbsent': '%d saved - not in this game',
        'AMF_McmMemForget': 'Forget',
        'AMF_McmMemBackingUp': 'Backing up...',
        'AMF_McmMemRestoring': 'Restoring...',
        'AMF_McmMemBackedUp': 'Backed up: %d settings from %d menus.',
        'AMF_McmMemRestored': 'Restored: %d settings set in %d menus (%d not found).',
    },
    'german': {
        'AMF_McmMemTitle': 'Einstellungsgedächtnis',
        'AMF_McmMemHelp': 'Ein neues Spiel vergisst, worauf Menüs, die nur im Skript einer Mod stehen, und MCM-Helper-Einstellungen, die in deinem Spielstand liegen, gestellt waren. Diese Einstellungen werden hier gemerkt und nach einem neuen Spiel wieder gesetzt. Die übrigen Einstellungen von MCM Helper bewahrt MCM Helper selbst.',
        'AMF_McmMemAuto': 'Jede Änderung hier merken',
        'AMF_McmMemOnNewGame': 'Nach einem neuen Spiel wieder setzen',
        'AMF_McmMemProfile': 'Profil',
        'AMF_McmMemNewName': 'Name des neuen Profils',
        'AMF_McmMemNew': 'Neu',
        'AMF_McmMemCopy': 'Dieses kopieren',
        'AMF_McmMemExists': 'Ein Profil mit diesem Namen gibt es schon.',
        'AMF_McmMemDelete': 'Ein Profil löschen...',
        'AMF_McmMemBackUp': 'Jetzt alles sichern',
        'AMF_McmMemRestore': 'Jetzt wiederherstellen',
        'AMF_McmMemWait': 'Schließe die offene Seite eines Mod-Konfigurationsmenüs oder warte, bis die laufende Sicherung oder Wiederherstellung fertig ist.',
        'AMF_McmMemButtonsHelp': 'Jetzt alles sichern liest jedes Menü, das dieses Menü lesen kann - auch was du in SkyUIs eigenem Menü eingestellt hast. Jetzt wiederherstellen setzt die Menüs dieses Spiels auf das Profil.',
        'AMF_McmMemMenus': 'Menüs in diesem Profil (%d)',
        'AMF_McmMemMenusHelp': 'Schalte ein Menü aus, um es aus der automatischen Wiederherstellung nach einem neuen Spiel herauszunehmen; Vergessen löscht, was dieses Profil davon gemerkt hat.',
        'AMF_McmMemSaved': '%d gemerkt',
        'AMF_McmMemAbsent': '%d gemerkt - nicht in diesem Spiel',
        'AMF_McmMemForget': 'Vergessen',
        'AMF_McmMemBackingUp': 'Wird gesichert...',
        'AMF_McmMemRestoring': 'Wird wiederhergestellt...',
        'AMF_McmMemBackedUp': 'Gesichert: %d Einstellungen aus %d Menüs.',
        'AMF_McmMemRestored': 'Wiederhergestellt: %d Einstellungen in %d Menüs gesetzt (%d nicht gefunden).',
    },
    'french': {
        'AMF_McmMemTitle': 'Mémoire des réglages',
        'AMF_McmMemHelp': "Une nouvelle partie oublie la valeur des menus écrits uniquement dans le script d'un mod, et des réglages MCM Helper gardés dans votre sauvegarde. Ces réglages sont mémorisés ici et réappliqués après une nouvelle partie. Les autres réglages de MCM Helper sont conservés par MCM Helper lui-même.",
        'AMF_McmMemAuto': 'Mémoriser chaque changement fait ici',
        'AMF_McmMemOnNewGame': 'Les réappliquer après une nouvelle partie',
        'AMF_McmMemProfile': 'Profil',
        'AMF_McmMemNewName': 'Nom du nouveau profil',
        'AMF_McmMemNew': 'Nouveau',
        'AMF_McmMemCopy': 'Copier celui-ci',
        'AMF_McmMemExists': 'Un profil de ce nom existe déjà.',
        'AMF_McmMemDelete': 'Supprimer un profil...',
        'AMF_McmMemBackUp': 'Tout sauvegarder maintenant',
        'AMF_McmMemRestore': 'Restaurer maintenant',
        'AMF_McmMemWait': "Fermez la page de menu de configuration de mod ouverte, ou attendez la fin de la sauvegarde ou de la restauration en cours.",
        'AMF_McmMemButtonsHelp': "Tout sauvegarder maintenant lit chaque menu que ce menu peut lire - y compris ce que vous avez réglé dans le menu de SkyUI. Restaurer maintenant règle les menus de cette partie sur le profil.",
        'AMF_McmMemMenus': 'Menus de ce profil (%d)',
        'AMF_McmMemMenusHelp': "Désactivez un menu pour l'exclure de la restauration automatique après une nouvelle partie ; Oublier efface ce que ce profil en a mémorisé.",
        'AMF_McmMemSaved': '%d mémorisés',
        'AMF_McmMemAbsent': '%d mémorisés - absent de cette partie',
        'AMF_McmMemForget': 'Oublier',
        'AMF_McmMemBackingUp': 'Sauvegarde...',
        'AMF_McmMemRestoring': 'Restauration...',
        'AMF_McmMemBackedUp': 'Sauvegardé : %d réglages de %d menus.',
        'AMF_McmMemRestored': 'Restauré : %d réglages appliqués dans %d menus (%d introuvables).',
    },
    'italian': {
        'AMF_McmMemTitle': 'Memoria delle impostazioni',
        'AMF_McmMemHelp': "Una nuova partita dimentica il valore dei menu scritti solo nello script di una mod e delle impostazioni di MCM Helper tenute nel salvataggio. Queste impostazioni vengono ricordate qui e reimpostate dopo una nuova partita. Le altre impostazioni di MCM Helper le conserva MCM Helper stesso.",
        'AMF_McmMemAuto': 'Ricorda ogni modifica fatta qui',
        'AMF_McmMemOnNewGame': 'Reimpostale dopo una nuova partita',
        'AMF_McmMemProfile': 'Profilo',
        'AMF_McmMemNewName': 'Nome del nuovo profilo',
        'AMF_McmMemNew': 'Nuovo',
        'AMF_McmMemCopy': 'Copia questo',
        'AMF_McmMemExists': 'Esiste già un profilo con quel nome.',
        'AMF_McmMemDelete': 'Elimina un profilo...',
        'AMF_McmMemBackUp': 'Salva tutto ora',
        'AMF_McmMemRestore': 'Ripristina ora',
        'AMF_McmMemWait': 'Chiudi la pagina del menu di configurazione delle mod aperta, o attendi il salvataggio o il ripristino in corso.',
        'AMF_McmMemButtonsHelp': "Salva tutto ora legge ogni menu che questo menu può leggere - anche ciò che hai impostato nel menu di SkyUI. Ripristina ora imposta i menu di questa partita sul profilo.",
        'AMF_McmMemMenus': 'Menu in questo profilo (%d)',
        'AMF_McmMemMenusHelp': 'Disattiva un menu per escluderlo dal ripristino automatico dopo una nuova partita; Dimentica cancella ciò che questo profilo ne ricorda.',
        'AMF_McmMemSaved': '%d salvate',
        'AMF_McmMemAbsent': '%d salvate - non in questa partita',
        'AMF_McmMemForget': 'Dimentica',
        'AMF_McmMemBackingUp': 'Salvataggio...',
        'AMF_McmMemRestoring': 'Ripristino...',
        'AMF_McmMemBackedUp': 'Salvate: %d impostazioni da %d menu.',
        'AMF_McmMemRestored': 'Ripristinate: %d impostazioni in %d menu (%d non trovate).',
    },
    'spanish': {
        'AMF_McmMemTitle': 'Memoria de ajustes',
        'AMF_McmMemHelp': 'Una partida nueva olvida el valor de los menús escritos solo en el script de un mod y de los ajustes de MCM Helper guardados en tu partida. Esos ajustes se recuerdan aquí y se vuelven a aplicar tras una partida nueva. Los demás ajustes de MCM Helper los guarda el propio MCM Helper.',
        'AMF_McmMemAuto': 'Recordar cada cambio hecho aquí',
        'AMF_McmMemOnNewGame': 'Volver a aplicarlos tras una partida nueva',
        'AMF_McmMemProfile': 'Perfil',
        'AMF_McmMemNewName': 'Nombre del perfil nuevo',
        'AMF_McmMemNew': 'Nuevo',
        'AMF_McmMemCopy': 'Copiar este',
        'AMF_McmMemExists': 'Ya existe un perfil con ese nombre.',
        'AMF_McmMemDelete': 'Borrar un perfil...',
        'AMF_McmMemBackUp': 'Guardar todo ahora',
        'AMF_McmMemRestore': 'Restaurar ahora',
        'AMF_McmMemWait': 'Cierra la página del menú de configuración de mod abierta, o espera a que termine la copia o la restauración en curso.',
        'AMF_McmMemButtonsHelp': 'Guardar todo ahora lee cada menú que este menú puede leer, también lo que ajustaste en el menú propio de SkyUI. Restaurar ahora pone los menús de esta partida según el perfil.',
        'AMF_McmMemMenus': 'Menús en este perfil (%d)',
        'AMF_McmMemMenusHelp': 'Desactiva un menú para dejarlo fuera de la restauración automática tras una partida nueva; Olvidar borra lo que este perfil recuerda de él.',
        'AMF_McmMemSaved': '%d guardados',
        'AMF_McmMemAbsent': '%d guardados - no está en esta partida',
        'AMF_McmMemForget': 'Olvidar',
        'AMF_McmMemBackingUp': 'Guardando...',
        'AMF_McmMemRestoring': 'Restaurando...',
        'AMF_McmMemBackedUp': 'Guardado: %d ajustes de %d menús.',
        'AMF_McmMemRestored': 'Restaurado: %d ajustes aplicados en %d menús (%d no encontrados).',
    },
    'polish': {
        'AMF_McmMemTitle': 'Pamięć ustawień',
        'AMF_McmMemHelp': 'Nowa gra zapomina wartości menu napisanych tylko w skrypcie moda oraz ustawień MCM Helper trzymanych w zapisie gry. Te ustawienia są tu zapamiętywane i ustawiane ponownie po nowej grze. Pozostałe ustawienia MCM Helper przechowuje sam MCM Helper.',
        'AMF_McmMemAuto': 'Zapamiętuj każdą zmianę zrobioną tutaj',
        'AMF_McmMemOnNewGame': 'Ustaw je ponownie po nowej grze',
        'AMF_McmMemProfile': 'Profil',
        'AMF_McmMemNewName': 'Nazwa nowego profilu',
        'AMF_McmMemNew': 'Nowy',
        'AMF_McmMemCopy': 'Skopiuj ten',
        'AMF_McmMemExists': 'Profil o tej nazwie już istnieje.',
        'AMF_McmMemDelete': 'Usuń profil...',
        'AMF_McmMemBackUp': 'Zapisz wszystko teraz',
        'AMF_McmMemRestore': 'Przywróć teraz',
        'AMF_McmMemWait': 'Zamknij otwartą stronę menu konfiguracji moda albo poczekaj na koniec trwającego zapisu lub przywracania.',
        'AMF_McmMemButtonsHelp': 'Zapisz wszystko teraz odczytuje każde menu, które to menu potrafi odczytać - także to, co ustawiono we własnym menu SkyUI. Przywróć teraz ustawia menu tej gry według profilu.',
        'AMF_McmMemMenus': 'Menu w tym profilu (%d)',
        'AMF_McmMemMenusHelp': 'Wyłącz menu, aby pominąć je w automatycznym przywracaniu po nowej grze; Zapomnij usuwa to, co ten profil o nim pamięta.',
        'AMF_McmMemSaved': 'zapamiętane: %d',
        'AMF_McmMemAbsent': 'zapamiętane: %d - nie ma go w tej grze',
        'AMF_McmMemForget': 'Zapomnij',
        'AMF_McmMemBackingUp': 'Zapisywanie...',
        'AMF_McmMemRestoring': 'Przywracanie...',
        'AMF_McmMemBackedUp': 'Zapisano: %d ustawień z %d menu.',
        'AMF_McmMemRestored': 'Przywrócono: %d ustawień w %d menu (nie znaleziono: %d).',
    },
    'czech': {
        'AMF_McmMemTitle': 'Paměť nastavení',
        'AMF_McmMemHelp': 'Nová hra zapomene hodnoty menu napsaných jen ve skriptu modu a nastavení MCM Helper uložených ve vaší pozici. Tato nastavení se zde pamatují a po nové hře se nastaví znovu. Ostatní nastavení MCM Helper si uchovává MCM Helper sám.',
        'AMF_McmMemAuto': 'Pamatovat si každou změnu provedenou zde',
        'AMF_McmMemOnNewGame': 'Po nové hře je nastavit znovu',
        'AMF_McmMemProfile': 'Profil',
        'AMF_McmMemNewName': 'Název nového profilu',
        'AMF_McmMemNew': 'Nový',
        'AMF_McmMemCopy': 'Zkopírovat tento',
        'AMF_McmMemExists': 'Profil s tímto názvem už existuje.',
        'AMF_McmMemDelete': 'Smazat profil...',
        'AMF_McmMemBackUp': 'Zálohovat vše teď',
        'AMF_McmMemRestore': 'Obnovit teď',
        'AMF_McmMemWait': 'Zavřete otevřenou stránku konfiguračního menu modu, nebo počkejte na dokončení probíhající zálohy či obnovy.',
        'AMF_McmMemButtonsHelp': 'Zálohovat vše teď přečte každé menu, které toto menu umí přečíst - i to, co jste nastavili ve vlastním menu SkyUI. Obnovit teď nastaví menu této hry podle profilu.',
        'AMF_McmMemMenus': 'Menu v tomto profilu (%d)',
        'AMF_McmMemMenusHelp': 'Vypněte menu, aby se vynechalo z automatické obnovy po nové hře; Zapomenout smaže, co si o něm tento profil pamatuje.',
        'AMF_McmMemSaved': 'uloženo: %d',
        'AMF_McmMemAbsent': 'uloženo: %d - v této hře není',
        'AMF_McmMemForget': 'Zapomenout',
        'AMF_McmMemBackingUp': 'Zálohuje se...',
        'AMF_McmMemRestoring': 'Obnovuje se...',
        'AMF_McmMemBackedUp': 'Zálohováno: %d nastavení z %d menu.',
        'AMF_McmMemRestored': 'Obnoveno: %d nastavení v %d menu (nenalezeno: %d).',
    },
    'russian': {
        'AMF_McmMemTitle': 'Память настроек',
        'AMF_McmMemHelp': 'Новая игра забывает значения меню, написанных только в скрипте мода, и настроек MCM Helper, которые хранятся в сохранении. Эти настройки запоминаются здесь и устанавливаются снова после новой игры. Остальные настройки MCM Helper хранит сам MCM Helper.',
        'AMF_McmMemAuto': 'Запоминать каждое изменение, сделанное здесь',
        'AMF_McmMemOnNewGame': 'Устанавливать их снова после новой игры',
        'AMF_McmMemProfile': 'Профиль',
        'AMF_McmMemNewName': 'Имя нового профиля',
        'AMF_McmMemNew': 'Новый',
        'AMF_McmMemCopy': 'Копия этого',
        'AMF_McmMemExists': 'Профиль с таким именем уже есть.',
        'AMF_McmMemDelete': 'Удалить профиль...',
        'AMF_McmMemBackUp': 'Сохранить всё сейчас',
        'AMF_McmMemRestore': 'Восстановить сейчас',
        'AMF_McmMemWait': 'Закройте открытую страницу меню настройки мода или дождитесь окончания идущего сохранения или восстановления.',
        'AMF_McmMemButtonsHelp': 'Сохранить всё сейчас читает каждое меню, которое может прочитать это меню, - и то, что вы настроили в собственном меню SkyUI. Восстановить сейчас ставит меню этой игры по профилю.',
        'AMF_McmMemMenus': 'Меню в этом профиле (%d)',
        'AMF_McmMemMenusHelp': 'Выключите меню, чтобы исключить его из автоматического восстановления после новой игры; «Забыть» удаляет то, что этот профиль о нём помнит.',
        'AMF_McmMemSaved': 'сохранено: %d',
        'AMF_McmMemAbsent': 'сохранено: %d - нет в этой игре',
        'AMF_McmMemForget': 'Забыть',
        'AMF_McmMemBackingUp': 'Сохранение...',
        'AMF_McmMemRestoring': 'Восстановление...',
        'AMF_McmMemBackedUp': 'Сохранено: %d настроек из %d меню.',
        'AMF_McmMemRestored': 'Восстановлено: %d настроек в %d меню (не найдено: %d).',
    },
    'chinese': {
        'AMF_McmMemTitle': '设置记忆',
        'AMF_McmMemHelp': '新游戏会忘记只写在模组脚本里的菜单，以及存放在存档里的 MCM Helper 设置原先的值。这些设置会记在这里，并在新游戏后重新设置。MCM Helper 的其他设置由 MCM Helper 自己保存。',
        'AMF_McmMemAuto': '记住在此所做的每项更改',
        'AMF_McmMemOnNewGame': '新游戏后重新设置',
        'AMF_McmMemProfile': '配置档',
        'AMF_McmMemNewName': '新配置档名称',
        'AMF_McmMemNew': '新建',
        'AMF_McmMemCopy': '复制此配置档',
        'AMF_McmMemExists': '已有同名配置档。',
        'AMF_McmMemDelete': '删除配置档...',
        'AMF_McmMemBackUp': '立即全部备份',
        'AMF_McmMemRestore': '立即恢复',
        'AMF_McmMemWait': '请关闭已打开的模组配置菜单页面，或等待正在进行的备份或恢复完成。',
        'AMF_McmMemButtonsHelp': '“立即全部备份”会读取本菜单能读取的每个菜单——包括你在 SkyUI 自己的菜单里设置的内容。“立即恢复”会把本局游戏的菜单设为配置档中的值。',
        'AMF_McmMemMenus': '此配置档中的菜单（%d）',
        'AMF_McmMemMenusHelp': '关闭某个菜单可将其排除在新游戏后的自动恢复之外；“忘记”会删除此配置档对它的记忆。',
        'AMF_McmMemSaved': '已记住 %d 项',
        'AMF_McmMemAbsent': '已记住 %d 项 - 不在本局游戏中',
        'AMF_McmMemForget': '忘记',
        'AMF_McmMemBackingUp': '正在备份...',
        'AMF_McmMemRestoring': '正在恢复...',
        'AMF_McmMemBackedUp': '已备份：%d 项设置，来自 %d 个菜单。',
        'AMF_McmMemRestored': '已恢复：在 %d 个菜单中设置了 %d 项（%d 项未找到）。',
    },
    'japanese': {
        'AMF_McmMemTitle': '設定の記憶',
        'AMF_McmMemHelp': '新しいゲームでは、モッドのスクリプトだけで書かれたメニューと、セーブに保存される MCM Helper の設定の値が失われます。それらの設定をここで記憶し、新しいゲームの後に再び設定します。MCM Helper のその他の設定は MCM Helper 自身が保持します。',
        'AMF_McmMemAuto': 'ここで行った変更をすべて記憶する',
        'AMF_McmMemOnNewGame': '新しいゲームの後に再設定する',
        'AMF_McmMemProfile': 'プロファイル',
        'AMF_McmMemNewName': '新しいプロファイル名',
        'AMF_McmMemNew': '新規',
        'AMF_McmMemCopy': 'これを複製',
        'AMF_McmMemExists': 'その名前のプロファイルは既にあります。',
        'AMF_McmMemDelete': 'プロファイルを削除...',
        'AMF_McmMemBackUp': '今すぐすべてバックアップ',
        'AMF_McmMemRestore': '今すぐ復元',
        'AMF_McmMemWait': '開いているモッド設定メニューのページを閉じるか、実行中のバックアップまたは復元の完了を待ってください。',
        'AMF_McmMemButtonsHelp': '「今すぐすべてバックアップ」は、このメニューが読めるすべてのメニューを読み取ります（SkyUI 自身のメニューで設定した内容も含む）。「今すぐ復元」はこのゲームのメニューをプロファイルの値に設定します。',
        'AMF_McmMemMenus': 'このプロファイルのメニュー（%d）',
        'AMF_McmMemMenusHelp': 'メニューをオフにすると、新しいゲーム後の自動復元から外れます。「忘れる」はこのプロファイルが記憶している内容を削除します。',
        'AMF_McmMemSaved': '%d 件記憶',
        'AMF_McmMemAbsent': '%d 件記憶 - このゲームにはありません',
        'AMF_McmMemForget': '忘れる',
        'AMF_McmMemBackingUp': 'バックアップ中...',
        'AMF_McmMemRestoring': '復元中...',
        'AMF_McmMemBackedUp': 'バックアップ完了：%d 件の設定（%d 個のメニュー）。',
        'AMF_McmMemRestored': '復元完了：%d 件の設定を %d 個のメニューで設定（%d 件見つからず）。',
    },
    'korean': {
        'AMF_McmMemTitle': '설정 기억',
        'AMF_McmMemHelp': '새 게임은 모드 스크립트로만 작성된 메뉴와 세이브에 저장되는 MCM Helper 설정의 값을 잊어버립니다. 이 설정들은 여기에 기억되고 새 게임 후 다시 설정됩니다. MCM Helper의 다른 설정은 MCM Helper가 직접 보관합니다.',
        'AMF_McmMemAuto': '여기서 한 변경을 모두 기억',
        'AMF_McmMemOnNewGame': '새 게임 후 다시 설정',
        'AMF_McmMemProfile': '프로필',
        'AMF_McmMemNewName': '새 프로필 이름',
        'AMF_McmMemNew': '새로 만들기',
        'AMF_McmMemCopy': '이 프로필 복사',
        'AMF_McmMemExists': '그 이름의 프로필이 이미 있습니다.',
        'AMF_McmMemDelete': '프로필 삭제...',
        'AMF_McmMemBackUp': '지금 모두 백업',
        'AMF_McmMemRestore': '지금 복원',
        'AMF_McmMemWait': '열려 있는 모드 설정 메뉴 페이지를 닫거나, 진행 중인 백업 또는 복원이 끝날 때까지 기다리세요.',
        'AMF_McmMemButtonsHelp': '지금 모두 백업은 이 메뉴가 읽을 수 있는 모든 메뉴를 읽습니다 - SkyUI 자체 메뉴에서 설정한 내용도 포함됩니다. 지금 복원은 이 게임의 메뉴를 프로필 값으로 설정합니다.',
        'AMF_McmMemMenus': '이 프로필의 메뉴 (%d)',
        'AMF_McmMemMenusHelp': '메뉴를 끄면 새 게임 후 자동 복원에서 제외됩니다. 잊기는 이 프로필이 그 메뉴에 대해 기억한 내용을 지웁니다.',
        'AMF_McmMemSaved': '%d개 기억됨',
        'AMF_McmMemAbsent': '%d개 기억됨 - 이 게임에 없음',
        'AMF_McmMemForget': '잊기',
        'AMF_McmMemBackingUp': '백업 중...',
        'AMF_McmMemRestoring': '복원 중...',
        'AMF_McmMemBackedUp': '백업됨: %d개 설정 (%d개 메뉴).',
        'AMF_McmMemRestored': '복원됨: %d개 메뉴에서 %d개 설정 적용 (%d개 찾지 못함).',
    },
}

# The restored line's arguments come as (settings, menus, missing) and snprintf has no positional %1$d, so Chinese,
# Japanese and Korean are worded to take them in that order.
S['chinese']['AMF_McmMemRestored'] = '已恢复：设置了 %d 项，涉及 %d 个菜单（%d 项未找到）。'
S['japanese']['AMF_McmMemRestored'] = '復元完了：%d 件の設定（%d 個のメニュー、%d 件見つからず）。'
S['korean']['AMF_McmMemRestored'] = '복원됨: %d개 설정 적용 (%d개 메뉴, %d개 찾지 못함).'

# 2.4.0: what the player changed comes back on every load (the owner, 2026-11-03).
S['english'].update({
    'AMF_McmMemOnLoad': 'Set what you changed again on every load',
    'AMF_McmMemOnLoadHelp': 'What you change on these menus comes back whenever you load a save or start a new game, in every save, the way a setting on an SKSE menu does. Only the rows you changed; a row already right is left alone. Rows that run something, like a reset or an uninstall, are never run again. Forget below drops what a menu remembers.',
    'AMF_McmMemYours': '%d you changed',
    'AMF_McmMemSettingNow': "Setting this menu's remembered values. One moment.",
})
S['german'].update({
    'AMF_McmMemOnLoad': 'Was du geändert hast, bei jedem Laden wieder setzen',
    'AMF_McmMemOnLoadHelp': 'Was du in diesen Menüs änderst, kommt zurück, wann immer du einen Spielstand lädst oder ein neues Spiel beginnst, in jedem Spielstand, so wie eine Einstellung in einem SKSE-Menü. Nur die Zeilen, die du geändert hast; eine Zeile, die schon stimmt, bleibt unberührt. Zeilen, die etwas ausführen, etwa Zurücksetzen oder Deinstallieren, werden nie erneut ausgeführt. Mit Vergessen unten verwirfst du, was ein Menü sich gemerkt hat.',
    'AMF_McmMemYours': '%d von dir geändert',
    'AMF_McmMemSettingNow': 'Die gemerkten Werte dieses Menüs werden gesetzt. Einen Moment.',
})
S['french'].update({
    'AMF_McmMemOnLoad': 'Réappliquer ce que vous avez changé à chaque chargement',
    'AMF_McmMemOnLoadHelp': "Ce que vous changez dans ces menus revient chaque fois que vous chargez une sauvegarde ou commencez une nouvelle partie, dans toutes les sauvegardes, comme un réglage d'un menu SKSE. Seulement les lignes que vous avez changées ; une ligne déjà correcte n'est pas touchée. Les lignes qui exécutent quelque chose, comme une réinitialisation ou une désinstallation, ne sont jamais relancées. Oublier, plus bas, efface ce qu'un menu a retenu.",
    'AMF_McmMemYours': '%d changés par vous',
    'AMF_McmMemSettingNow': 'Application des valeurs retenues de ce menu. Un instant.',
})
S['italian'].update({
    'AMF_McmMemOnLoad': 'Reimposta ciò che hai cambiato a ogni caricamento',
    'AMF_McmMemOnLoadHelp': "Ciò che cambi in questi menu torna ogni volta che carichi un salvataggio o inizi una nuova partita, in ogni salvataggio, come un'impostazione di un menu SKSE. Solo le righe che hai cambiato; una riga già giusta non viene toccata. Le righe che eseguono qualcosa, come un ripristino o una disinstallazione, non vengono mai rieseguite. Dimentica, qui sotto, elimina ciò che un menu ricorda.",
    'AMF_McmMemYours': '%d cambiate da te',
    'AMF_McmMemSettingNow': 'Impostazione dei valori ricordati di questo menu. Un momento.',
})
S['spanish'].update({
    'AMF_McmMemOnLoad': 'Volver a aplicar lo que cambiaste en cada carga',
    'AMF_McmMemOnLoadHelp': 'Lo que cambias en estos menús vuelve cada vez que cargas una partida o empiezas una nueva, en todas las partidas, como un ajuste de un menú SKSE. Solo las filas que cambiaste; una fila que ya está bien no se toca. Las filas que ejecutan algo, como un reinicio o una desinstalación, nunca se vuelven a ejecutar. Olvidar, más abajo, borra lo que un menú recuerda.',
    'AMF_McmMemYours': '%d cambiados por ti',
    'AMF_McmMemSettingNow': 'Aplicando los valores recordados de este menú. Un momento.',
})
S['polish'].update({
    'AMF_McmMemOnLoad': 'Ustawiaj ponownie to, co zmieniono, przy każdym wczytaniu',
    'AMF_McmMemOnLoadHelp': 'To, co zmieniasz w tych menu, wraca przy każdym wczytaniu zapisu lub rozpoczęciu nowej gry, w każdym zapisie, tak jak ustawienie w menu SKSE. Tylko wiersze, które zmieniono; wiersz już poprawny pozostaje nietknięty. Wiersze, które coś uruchamiają, na przykład reset lub odinstalowanie, nigdy nie są uruchamiane ponownie. Zapomnij poniżej usuwa to, co menu zapamiętało.',
    'AMF_McmMemYours': 'zmienione przez Ciebie: %d',
    'AMF_McmMemSettingNow': 'Ustawianie zapamiętanych wartości tego menu. Chwileczkę.',
})
S['czech'].update({
    'AMF_McmMemOnLoad': 'Při každém načtení znovu nastavit, co jste změnili',
    'AMF_McmMemOnLoadHelp': 'Co v těchto nabídkách změníte, se vrátí při každém načtení uložené hry nebo začátku nové hry, v každé uložené hře, stejně jako nastavení v nabídce SKSE. Jen řádky, které jste změnili; řádek, který už souhlasí, zůstane nedotčen. Řádky, které něco spouštějí, například reset nebo odinstalaci, se znovu nikdy nespustí. Zapomenout níže zahodí, co si nabídka pamatuje.',
    'AMF_McmMemYours': 'změněno vámi: %d',
    'AMF_McmMemSettingNow': 'Nastavují se zapamatované hodnoty této nabídky. Okamžik.',
})
S['russian'].update({
    'AMF_McmMemOnLoad': 'Снова устанавливать изменённое при каждой загрузке',
    'AMF_McmMemOnLoadHelp': 'То, что вы меняете в этих меню, возвращается при каждой загрузке сохранения или начале новой игры, в любом сохранении, как настройка в меню SKSE. Только изменённые вами строки; строка с верным значением не затрагивается. Строки, которые что-то запускают, например сброс или удаление, никогда не запускаются снова. Кнопка «Забыть» ниже стирает то, что запомнило меню.',
    'AMF_McmMemYours': 'изменено вами: %d',
    'AMF_McmMemSettingNow': 'Устанавливаются запомненные значения этого меню. Минуту.',
})
S['chinese'].update({
    'AMF_McmMemOnLoad': '每次读取时重新设置你更改过的内容',
    'AMF_McmMemOnLoadHelp': '你在这些菜单中更改的内容，会在每次读取存档或开始新游戏时恢复，适用于所有存档，就像 SKSE 菜单中的设置一样。只恢复你更改过的行；已经正确的行不会被改动。执行操作的行（例如重置或卸载）绝不会再次运行。下方的“忘记”会清除某个菜单记住的内容。',
    'AMF_McmMemYours': '你更改了 %d 项',
    'AMF_McmMemSettingNow': '正在设置此菜单记住的值，请稍候。',
})
S['japanese'].update({
    'AMF_McmMemOnLoad': '変更した内容をロードのたびに再設定する',
    'AMF_McmMemOnLoadHelp': 'これらのメニューで変更した内容は、セーブをロードしたり新しいゲームを始めたりするたびに、どのセーブでも、SKSE メニューの設定と同じように戻ります。変更した行だけが対象で、すでに正しい行には触れません。リセットやアンインストールなど何かを実行する行は、二度と実行されません。下の「忘れる」で、メニューが覚えている内容を消せます。',
    'AMF_McmMemYours': '変更した項目 %d 件',
    'AMF_McmMemSettingNow': 'このメニューの記憶した値を設定しています。少々お待ちください。',
})
S['korean'].update({
    'AMF_McmMemOnLoad': '변경한 내용을 불러올 때마다 다시 설정',
    'AMF_McmMemOnLoadHelp': '이 메뉴에서 변경한 내용은 저장을 불러오거나 새 게임을 시작할 때마다, 모든 저장에서, SKSE 메뉴의 설정처럼 되돌아옵니다. 변경한 줄만 해당하며, 이미 맞는 줄은 건드리지 않습니다. 초기화나 제거처럼 무언가를 실행하는 줄은 다시 실행되지 않습니다. 아래의 잊기를 누르면 메뉴가 기억하는 내용이 지워집니다.',
    'AMF_McmMemYours': '변경한 항목 %d개',
    'AMF_McmMemSettingNow': '이 메뉴에 기억된 값을 설정하는 중입니다. 잠시만 기다려 주세요.',
})


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
            if k in table and table[k].count('%d') != S['english'][k].count('%d'):
                print(f'{lang} {k}: %d count differs from English')
                bad += 1
        path = os.path.join(DIR, f'ApocryphaMenuFramework_{lang}.txt')
        raw = open(path, 'rb').read()
        text = raw.decode('utf-16')
        have = {l.split('\t', 1)[0] for l in text.splitlines() if '\t' in l}
        add = [k for k in keys if '$' + k not in have and k in table]
        if check or not add:
            print(f'{lang}: {len(add)} to add')
            continue
        tail = '' if text.endswith('\r\n') or text.endswith('\n') else '\r\n'
        text += tail + ''.join(f'${k}\t{table[k]}\r\n' for k in add)
        open(path, 'wb').write(b'\xff\xfe' + text.lstrip('﻿').encode('utf-16-le'))
        print(f'{lang}: {len(add)} added')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
