# Undertale — Cheat Engine Rehberi

## SOUL Koordinatlarını Bulma

Cheat Engine'in en temel işlemi: "değişen bir sayıyı bulma" mantığı.

### 1. Hazırlık
- Undertale'i aç, bir savaşa gir (herhangi bir düşman, önemli değil)
- Cheat Engine'i aç, sol üstteki bilgisayar simgesine tıkla → process listesinden `UNDERTALE.exe`'yi seç ve **Open**

### 2. X Koordinatını Bulma
1. Value Type'ı **Double** yap (GameMaker 1.4, Undertale dahil, *tüm* sayıları 8 baytlık double olarak tutar — Float ile ararsan hiçbir şey bulamazsın)
2. Kalbi (SOUL) tam ortaya getir, hareket ettirme
3. Değeri bilmediğin için Scan Type'ı **Unknown initial value** yap ve **First Scan**'e bas
4. Kalbi sadece sağa biraz oynat (sol/sağ tuşla kısa bir dokunuş)
5. Scan Type'ı **Increased value** yap (sağa gitmek genelde X'i artırır), **Next Scan**'e bas
6. Kalbi sola oynat
7. Scan Type'ı **Decreased value** yap, **Next Scan**'e bas
8. Bunu birkaç kez tekrarla (sağa→increased, sola→decreased) — her seferinde sonuç listesi daralır

Genelde 1-5 tekrarda tek bir adrese (ya da birkaç adrese) düşersin.

### 3. Y Koordinatını Bulma

Aynı yöntem, ama bu sefer:
- Kalbi yukarı oynat → **Increased value**
- Kalbi aşağı oynat → **Decreased value**
- Tekrarla, tek adrese düşene kadar

### 4. Doğrulama

Bulduğun adresi çift tıkla, alt listeye eklenir. Kalbi elle hareket ettir, listede o değerin gerçek zamanlı değiştiğini gör — değişiyorsa doğru adresi bulmuşsun demektir.

### 5. Adresi Kalıcı Hale Getirme (Önemli!)

Bulduğun ham adres (örn. `004A2F10`) her oyunu yeniden başlattığında değişebilir çünkü bu bir "dinamik adres". Kalıcı kullanmak için:
1. Adrese sağ tık → **Pointer scan for this address** → varsayılan ayarlarla tara
2. Oyunu kapatıp aç, adresi yeniden bul, pointer scan penceresinde **Pointer scanner → Rescan memory** ile yeni adresi ver
3. Birkaç yeniden başlatmadan sonra hâlâ doğru değeri gösteren pointer kalıcıdır — onu tabloya ekle

(**Find out what accesses this address** ise hangi kodun adrese dokunduğunu gösterir; pointer değil, kod enjeksiyonu için kullanılır.)

---

## Genel Mantık: Invincibility, Auto-Dodge ve Benzeri Özellikler

Bu bölüm, hangi oyun olursa olsun (Undertale ya da başka bir single-player oyun) aynı prensiple kendi başına adres bulmanı sağlayacak genel mantığı anlatır.

### Temel Mantık: "Bilinmeyen Bir Kutuyu Bulma" Oyunu

Bir oyun çalışırken, oyunun tüm verileri (HP, konum, mermi sayısı, altın, vs.) bilgisayarın RAM'inde milyonlarca farklı "kutuda" (adreste) saklanır. Sen hangi kutunun hangi veriye ait olduğunu bilmiyorsun. Cheat Engine'in yaptığı şey: o kutuyu, davranışına bakarak ayıklamak.

### Yöntem 1: "Değişimi İzle"

*En temel teknik — HP, Gold, mermi sayısı gibi statik sayılar için.*

**Mantık:**
1. RAM'de milyonlarca kutu var, hepsini tara (First Scan)
2. Oyunda bildiğin bir şeyi değiştir (hasar al, altın harca)
3. "Değeri artan/azalan/şuna eşit olan kutuları" filtrele (Next Scan)
4. Bunu 3-5 kez tekrarla → her seferinde yanlış kutular elenir, doğru kutu(lar) kalır

**Neden işe yarıyor?** Çünkü milyonlarca kutudan sadece gerçekten o veriyi tutan kutu senin yaptığın değişikliğe her seferinde doğru tepki verir. Rastgele kutular tesadüfen bir-iki kez uyar ama arka arkaya 4-5 kez tutmaz.

**Invincibility mantığı budur:** HP kutusunu bulduktan sonra, o kutuyu "dondurursun" (Freeze) → oyun kodu "HP -= 5" yapmaya çalışsa bile, Cheat Engine her milisaniyede o kutuya eski değeri geri yazar, oyun motoru bunu asla göremez.

### Yöntem 2: "Kim Bu Kutuya Dokunuyor?"

*Konum, koordinat gibi sürekli değişen veriler için.*

X/Y koordinatı gibi sürekli değişen şeylerde "artan/azalan" filtrelemesi zor çalışır çünkü değer zaten her frame değişiyor. Bunun yerine:

1. Kutuyu kabaca bulduktan sonra (yön bazlı artan/azalan taramasıyla), o kutuya sağ tık → **Find out what accesses/writes this address**
2. Cheat Engine, o RAM adresine hangi kod satırının (assembly instruction) yazdığını gerçek zamanlı yakalar
3. O kod satırını bulunca, oyunun "SOUL'un X'ini güncelleme" fonksiyonunu bulmuş olursun — bu sana pointer (kalıcı, oyun yeniden başlasa bile bulunabilen adres) çıkarma imkanı verir

**Auto-dodge mantığı budur:**
1. Önce SOUL'un konumunu (Yöntem 1 + 2 ile) bulursun
2. Sonra aynı mantıkla mermilerin konumunu tutan kutuları bulursun (mermiler bir liste/dizi halinde art arda RAM'de dururlar — buna "structure" denir)
3. Sonra kendi yazdığın küçük bir kod (script), her frame'de:
   - SOUL'un kutusunu okur
   - Mermilerin kutularını okur
   - Aradaki mesafeyi hesaplar (basit geometri: `√((x1-x2)² + (y1-y2)²)`)
   - Mesafe tehlikeli kadar azsa, SOUL'un kutusuna sen yeni bir koordinat yazarsın (oyunun tuş girdisini beklemeden, direkt zorla)

### Neden Auto-Dodge Zor, Invincibility Kolay?

| | Invincibility | Auto-dodge |
|---|---|---|
| Kaç kutu bulman lazım | 1 (HP) | Onlarca (SOUL + her mermi) |
| Ne yapıyorsun | Sadece "dondur" | Matematik hesabı + sürekli yazma |
| Veri yapısı | Tek sayı | Liste/dizi (structure) — her elemanın nerede başladığını bulman lazım |

### Özet Mantık Zinciri

**Bul (scan) → Doğrula (davranışa bak) → Kalıcılaştır (pointer çıkar) → Kullan (dondur ya da script ile oku/yaz)**

Bu dört adım, hangi oyun/hangi özellik olursa olsun (invincibility, sınırsız mermi, hız hilesi, auto-dodge) hep aynı. Fark sadece kaç kutu bulman gerektiği ve bulduğun veriyle ne yaptığın.

---

## Hile Menüsü (DLL) — şu an kullanılan

`utcheat\bin\UTInjector.exe`'yi çalıştır, oyunda **INSERT** ile menüyü aç. Ölümsüzlük, altın, Max HP, oyun hızı ve canlı SOUL/Frisk koordinatları var. Oyun dosyalarına dokunmaz. Ayrıntılar: [utcheat/README.md](utcheat/README.md).

---

## Alternatif: data.win Yaması (şu an kurulu değil)

`data.win` orijinal haline döndürüldü. Bu yama istenirse aşağıdaki "Yeniden uygulamak için" komutuyla tekrar kurulabilir. DLL ile aynı anda kullanma, ikisi de sol üste çizer.

| Tuş | Hile | Nasıl çalışıyor |
|---|---|---|
| **F5** | God mode | HP her karede `global.maxhp`'ye çekilir; `scr_gameoverb` (oyun bitti script'i) devre dışı kalır |
| **F6** | +100 altın | `global.gold += 100` |
| **F7** | Yavaş çekim | `room_speed` 30 → 15; mermiler yarı hızda gelir |
| **F8** | Bilgi paneli aç/kapa | Sol üstte SOUL / Frisk X-Y koordinatı, HP ve altın gösterir |

**Dosyalar**
- `undertale-cheats.csx` — yamanın kaynağı (UndertaleModTool script'i)
- `UTMT_CLI\` — yamayı uygulayan araç (UndertaleModTool 0.9.2.0 CLI)
- `data.win.orig` — orijinal oyun dosyası (bir kopyası da oyun klasöründe)

**Geri almak için:** `C:\Program Files (x86)\Steam\steamapps\common\Undertale\data.win.orig` dosyasını `data.win` olarak geri kopyala ya da Steam → Undertale → Özellikler → Yüklü Dosyalar → **Oyun dosyalarının bütünlüğünü doğrula**.

**Yeniden uygulamak için** (Steam güncellemesi dosyayı ezerse):
```
UTMT_CLI\UndertaleModCli.exe load "C:\Program Files (x86)\Steam\steamapps\common\Undertale\data.win" -s undertale-cheats.csx -o "C:\Program Files (x86)\Steam\steamapps\common\Undertale\data.win" -f
```

**Not:** F6 ile eklenen altın kayda yazılır. God mode açıkken kazanılan EXP/LV de normal şekilde kaydedilir.
