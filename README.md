# Undertale Hileleri

Undertale (Steam, Windows) için hile araçları.

## İndir
**[Son sürüm → UTCheat.zip](https://github.com/kmertkun/undertale-utcheat/releases/latest/download/UTCheat.zip)** (derlemeye gerek yok)

1. Zip'i bir klasöre aç.
2. `UTInjector.exe`'ye çift tıkla (oyun kapalıysa Steam'den kendisi açar).
3. Oyunda **INSERT** ile hile menüsünü aç/kapat.

Windows Defender / tarayıcı "bilinmeyen uygulama" uyarısı verebilir: enjektör başka bir işleme DLL yüklediği için bu tür araçlar sık işaretlenir. İçinden emin olmak istersen kaynaktan kendin derle ([utcheat/README.md](utcheat/README.md)).

## Özellikler

| | |
|---|---|
| **THE ERASURE** | Kırmızı buton: istediğin an Chara gibi dünyayı sil. Vuruş sesi, ekranı dolduran kırmızı 9'lar, pencere sallanır, oyun kapanır. Kayıtların silinmez, Steam Cloud'a bir şey yazılmaz |
| **Zamanı durdur (T)** | ZA WARUDO: senden başka her şey donar (mermiler, düşmanlar, NPC'ler, attığın mızraklar). Sen hareket etmeye devam edersin. Tekrar T: herkes kaldığı yerden devam eder |
| **Gaster Blaster / Undyne mızrağı** | Sans'ın Blaster'ını ya da Undyne'ın mızrağını çal: ekranda istediğin yere tıkla, oraya ateş etsin. Değdiği her şey 1 hasar alır: düşmanlar, Sans, savaşta kalp, haritada Frisk |
| **Karakter** | Haritada Frisk yerine Chara, Sans, Papyrus, Toriel, Undyne, Alphys, Asgore, Asriel, Monster Kid ya da Napstablook olarak gez |
| **Tek vuruş** | Varsayılan açık. Her vuruş 999999999 hasar, Sans dahil (MISS yok) |
| **Auto-dodge** | Kalp mermilerden kendi kaçar. Kırmızı, mavi, yeşil ve mor ruhun hepsinde çalışır. Rage (ışınlanarak no-hit) ya da Legit (yürüyerek) |
| **Sonlar** | Nötr (Sans'ın telefonu), Gerçek Pasifist (gün batımı → Toriel'in odası) ya da Soykırım (Chara'nın The Erasure sahnesi) sonuna ışınlan. Jenerik atlanır, kayıtlar önce otomatik yedeklenir |
| **Ölümsüzlük** | HP hep dolu, game over yok |
| **Test savaşı** | Haritadayken istediğin düşmanla savaş başlat (Sans, Undyne, Muffet, Papyrus...) |
| **Diğer** | Max HP, altın, oyun hızı (0.1x – 3x), canlı konum |

Enjekte edince ekranda "Ruhsuz adam, Undertale'e bile hile ha?" yazısı çıkar, yüklendiğini oradan anlarsın.

## Repoda neler var

| Klasör / dosya | Ne |
|---|---|
| [utcheat/](utcheat/) | Yukarıdaki özelliklerin kaynağı: oyuna enjekte edilen DLL (ImGui menüsü) ve enjektör. Nasıl çalıştığı ve derleme: [utcheat/README.md](utcheat/README.md) |
| [undertale-cheat-engine-rehberi.md](undertale-cheat-engine-rehberi.md) | Cheat Engine ile elle hile rehberi |
| [undertale-cheats.csx](undertale-cheats.csx) | UndertaleModTool ile `data.win` yaması (alternatif yöntem) |

Oyun dosyaları (`data.win` vb.) repoda yoktur; kendi Steam kopyanı kullan.

## Neden böyle bir oyuna hile?

Evet, Undertale'e hile yaptım. Merhamet üzerine kurulu, "kimseyi öldürmek zorunda değilsin" diyen bir oyuna tek vuruş hilesi. Farkındayım.

Ama:
- **Sans.** Tek başına yeterli bir sebep.
- Üç sonu görmek için oyunu üç kez baştan oynamak yerine bir butona basmak istedim.
- Asıl eğlencesi hilenin kendisi değil, yapımıydı: GameMaker'ın içini kurcalamak, mermileri okuyup kalbi kendi kendine kaçıran bir auto-dodge yazmak, Sans'ın "MISS"ini atlatmanın yolunu bulmak.
- Hep Frisk'le oynamaktan sıkıldım. Artık Sans olarak gezebiliyorum.
- Sans'ın Gaster Blaster'ını çaldım. Artık kendisine karşı kullanıyorum.
- Tek oyunculu bir oyun. Kimseye zararı yok; olsa olsa Flowey'ye.

The Erasure'da dünya yine silinip oyun kapanıyor, ama oyunun o anda kayıtları silip Steam Cloud'a kalıcı "ruhsuz" işareti bıraktığı kısım etkisiz. Yani bu hile oyunun kendisinden daha merhametli.

## Lisans

[MIT](LICENSE). `utcheat/third_party/` altındaki Dear ImGui (MIT) ve MinHook (BSD-2) kendi lisanslarıyla gelir.
