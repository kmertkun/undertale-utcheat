# UTCheat — Undertale Hile Menüsü (DLL)

Oyunun içine çizilen bir hile penceresi (Dear ImGui + Direct3D 9 hook). Oyun dosyalarına dokunmaz.

## Kullanım
Hazır derlenmiş hali: [Releases](https://github.com/kmertkun/undertale-utcheat/releases/latest) → `UTCheat.zip`. Kendin derlediysen:

1. `bin\UTInjector.exe`'yi çalıştır (oyun kapalıysa Steam'den kendisi açar).
2. Oyunda **INSERT** ile menüyü aç/kapat.

İstersen enjektör yerine Cheat Engine → Memory View → Tools → **Inject DLL** ile `bin\utcheat.dll`'i de yükleyebilirsin.

## Menü
| Bölüm | Özellik |
|---|---|
| THE ERASURE | Kırmızı buton: istediğin an Chara'nın dünyayı silmesi (vuruş sesi, ekranı dolduran 9'lar, pencere sallanıp oyun kapanır). Oyunun o anda yaptığı kayıt silme, ruhsuz işareti ve Steam Cloud yazımı etkisizleştirilir; kayıtlar yerinde kalır |
| Karakter | Haritada Frisk yerine Chara, Sans, Papyrus, Toriel, Undyne, Alphys, Asgore, Asriel, Monster Kid ya da Napstablook olarak gez. Çarpışma kutusu Frisk'te kalır, sprite ayaklara hizalanır |
| Can | Ölümsüzlük (HP hep dolu + game over engeli), tek vuruş (varsayılan açık, her vuruş 999999999; Sans dahil), canı doldur, Max HP ayarla |
| Sonlar | Haritadayken Nötr (Sans'ın telefonu), Gerçek Pasifist (gün batımı → Toriel'in odası) ya da Soykırım (Chara) sonunun son sahnesine ışınlar. Jenerik (teşekkürler) varsayılan olarak atlanır. Önce `%LOCALAPPDATA%\UNDERTALE` → `UNDERTALE_utcheat_yedek` yedeklenir; "Kayıtları geri yükle" ile geri alınır. Soykırımda Chara'nın "The Erasure" sahnesi (ERASE / DO NOT, ekranı dolduran 9'lar, pencere sallanıp oyun kapanır) olduğu gibi oynar; oyunun o anda yaptığı kayıt silme, `system_information_962` (ruhsuz işareti) ve Steam Cloud yazımı etkisizleştirilir |
| Altın | +100 / +1000, istediğin değere ayarla |
| Otomatik kaçış | Auto-dodge: mermilere yaklaşan kalbi en yakın güvenli noktaya taşır. "Kutuları göster" mermi/kalp/kutu sınırlarını çizer |
| Test savaşı | Haritadayken seçilen normal düşman grubuyla savaş başlatır (auto-dodge denemek için) |
| Oyun hızı | 0.1x – 3x (yavaş çekim / hızlandırma) |
| Konum | SOUL (savaşta) ve Frisk X/Y koordinatları, canlı |

## Nasıl çalışıyor
- `UNDERTALE.exe` ASLR kullanmıyor, bu yüzden GameMaker runner'ının iç fonksiyonları sabit adreslerde. Adresler exe'deki `Function_Add("variable_global_get", …)` kayıt çağrılarından çıkarıldı ([src/gm.h](src/gm.h)).
- Global değişkenler (`hp`, `maxhp`, `gold`, `lv`) runner'ın kendi slot-bul / oku / yaz fonksiyonlarıyla okunur ve yazılır.
- Ölümsüzlük: her karede `hp = maxhp` yapılır, ayrıca `script_execute` hook'u `scr_gameoverb`'yi (tüm game over yolları) engeller.
- Oyun hızı: `QueryPerformanceCounter` / `timeGetTime` / `GetTickCount` ölçeklenir (Cheat Engine'in speedhack'iyle aynı yöntem).
- Auto-dodge ([src/autododge.cpp](src/autododge.cpp)): kalbe (ya da mor ruhta `obj_purpleheart`'a) hasar verebilen 182 obje ([src/hazards.h](src/hazards.h), data.win'den üretildi) her karede taranır; kutuları ve kareden kareye hızları okunup birkaç kare sonrası tahmin edilir. Bbox yerine kodla çarpışan mermiler için gerçek hasar alanı kullanılır: Papyrus/Sans kemikleri, Sans'ın yerden çıkan kemik duvarı (uyarı süresi dahil), Gaster Blaster ışını (`idealx/idealy/idealrot` ve `alarm[4]` ile ateş zamanı).
  - **Doğal hareket (varsayılan):** kalp mermilerin içinden geçmez. Izgarada mermili hücrelerden kaçınan en kısa yolu (Dijkstra) bulur ve karede en fazla "Hız" kadar yürür; mermili hücreden sadece zaten içindeyse dışarı çıkmak için geçer. Kapatılırsa en yakın güvenli noktaya ışınlanır.
  - **Ruh modları:** kırmızı/sarı (serbest), mavi (yerçekimi; taşınınca "havada" durumuna alınır ki doğal düşsün), mor (Muffet: sadece ipliklerin üzerinde), yeşil (Undyne: kalkanın `idealdir`'i en yakın mızrağa çevrilir).
  - Sadece düşman sırasında (`global.mnfight == 2`) çalışır. Her kaçış/hasar `bin\utcheat.log`'a yazılır.
- Koordinat: runner'ın id→instance tablosu taranır, `object_index`'i `obj_heart` (744) / `obj_mainchara` (1576) olan instance'ın `x`/`y`'si okunur.

Sadece Steam sürümüyle (v1.08+) çalışır. Farklı bir exe'de DLL uyarı verir ve hiçbir şeye dokunmaz.

## Derleme
`build.bat` → `bin\utcheat.dll`, `bin\UTInjector.exe`. Derleyici `toolchain\` içinde (llvm-mingw, 32-bit hedef).
Oyun açıkken DLL kilitli olur, derlemeden önce oyunu kapat.
