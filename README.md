# Undertale Hileleri

Undertale (Steam, Windows) için hile araçları.

## İndir
**[Son sürüm → UTCheat.zip](https://github.com/kmertkun/undertale-utcheat/releases/latest/download/UTCheat.zip)** (derlemeye gerek yok)

1. Zip'i bir klasöre aç.
2. `UTInjector.exe`'ye çift tıkla (oyun kapalıysa Steam'den kendisi açar).
3. Oyunda **INSERT** ile hile menüsünü aç/kapat.

Windows Defender / tarayıcı "bilinmeyen uygulama" uyarısı verebilir: enjektör başka bir işleme DLL yüklediği için bu tür araçlar sık işaretlenir. İçinden emin olmak istersen kaynaktan kendin derle ([utcheat/README.md](utcheat/README.md)).

| Klasör / dosya | Ne |
|---|---|
| [utcheat/](utcheat/) | Oyuna enjekte edilen DLL: oyun içi ImGui menüsü, karakter değiştirme, ölümsüzlük, tek vuruş, auto-dodge (tüm ruh modları), test savaşı, sonlara ışınlanma, oyun hızı. Ayrıntılar ve derleme: [utcheat/README.md](utcheat/README.md) |
| [undertale-cheat-engine-rehberi.md](undertale-cheat-engine-rehberi.md) | Cheat Engine ile elle hile rehberi |
| [undertale-cheats.csx](undertale-cheats.csx) | UndertaleModTool ile `data.win` yaması (alternatif yöntem) |

Oyun dosyaları (`data.win` vb.) repoda yoktur; kendi Steam kopyanı kullan.

## Neden böyle bir oyuna hile?

Evet, Undertale'e hile yaptım. Merhamet üzerine kurulu, "kimseyi öldürmek zorunda değilsin" diyen bir oyuna tek vuruş hilesi. Farkındayım.

Ama:
- **Sans.** Tek başına yeterli bir sebep.
- Üç sonu görmek için oyunu üç kez baştan oynamak yerine bir butona basmak istedim.
- Asıl eğlencesi hilenin kendisi değil, yapımıydı: GameMaker'ın içini kurcalamak, mermileri okuyup kalbi kendi kendine kaçıran bir auto-dodge yazmak, Sans'ın "MISS"ini atlatmanın yolunu bulmak.
- Tek oyunculu bir oyun. Kimseye zararı yok; olsa olsa Flowey'ye.

Soykırım sonunda oyunun kayıtları silip Steam Cloud'a kalıcı işaret bıraktığı adım bile engelleniyor. Yani bu hile oyunun kendisinden daha merhametli.

## Lisans

[MIT](LICENSE). `utcheat/third_party/` altındaki Dear ImGui (MIT) ve MinHook (BSD-2) kendi lisanslarıyla gelir.
