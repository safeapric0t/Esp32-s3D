# Ender 3 S1 ESP32-S3 Controller

Ender 3 S1 Marlin yazıcıyı ESP32-S3 üzerinden USB Host ile ağa bağlayan açık kaynaklı uzaktan kontrol projesi.

Bu proje ile:

- nozzle ve tabla sıcaklıklarını izleyebilir,
- X/Y/Z konumunu görebilir,
- auto home ve jog hareketleri gönderebilir,
- manuel G-code çalıştırabilir,
- yazıcının SD kartındaki dosyaları listeleyip başlatabilir,
- G-code dosyasını ESP32’ye yükleyip SD kart olmadan ESP32 üzerinden yazdırabilir,
- baskı durumu ve ilerlemesini web arayüzünden izleyebilirsiniz.

> Proje yerel ağ kullanımı için tasarlanmıştır. İnternete doğrudan açmayın.

## Donanım

- Creality Ender 3 S1 veya uyumlu Marlin tabanlı yazıcı
- ESP32-S3 N16R8 kartı (16 MB Flash, 8 MB PSRAM)
- Veri aktarımı destekleyen USB-C kablosu
- Firmware yüklemek için uygun USB bağlantısı

ESP32-S3 üzerindeki USB Host/OTG USB-C portunu yazıcının USB portuna bağlayın. Yalnızca şarj destekleyen kablo kullanmayın.

## Çalışma mantığı

```text
Web tarayıcısı
      │ HTTP REST + WebSocket
      ▼
ESP32-S3
      │ USB Host CDC/ACM
      ▼
Ender 3 S1 Marlin
```

Dosya kaynakları ikiye ayrılır:

| Dosya kaynağı | Baskı yöntemi | SD kart gerekir mi? |
|---|---|---:|
| Yazıcının SD kartı | Marlin `M23` + `M24` | Evet |
| ESP32 SPIFFS | USB üzerinden G-code stream | Hayır |

ESP32’ye yüklenen dosya, SD karttan bağımsız olarak ESP32 üzerinde saklanır ve `stream` yöntemiyle yazıcıya satır satır gönderilir.

## Depoyu indirme

```bash
git clone https://github.com/safeapric0t/Esp32-s3D.git
cd Esp32-s3D
```

Alternatif olarak GitHub sayfasından **Code → Download ZIP** seçilebilir.

## Gerekli yazılımlar

- VS Code ve PlatformIO eklentisi veya PlatformIO CLI
- Node.js ve npm
- ESP32-S3 için USB sürücüsü

VS Code kullanıyorsanız projeyi `platformio.ini` dosyasının bulunduğu ana klasörden açın.

## Wi-Fi ayarı

Varsayılan yapılandırma erişim noktasıdır. Bu modda ev Wi-Fi bilgisi girmeniz gerekmez:

```text
Wi-Fi adı:   Ender3-Controller
Şifre:       ender3controller
Web adresi:  http://192.168.4.1
```

Bilgisayarınızı veya telefonunuzu bu ağa bağladıktan sonra tarayıcıda `http://192.168.4.1` adresini açın.

### ESP32’yi ev Wi-Fi ağına bağlama

Ev ağınızdan erişmek istiyorsanız proje içinde `include/local_config.h` adında yeni bir dosya oluşturun:

```cpp
#define LOCAL_WIFI_SSID "EV_WIFI_ADI"
#define LOCAL_WIFI_PASSWORD "EV_WIFI_SIFRESI"
#define LOCAL_AP_MODE false
```

Dosya yolu tam olarak şu olmalıdır:

```text
Esp32-s3D/include/local_config.h
```

Bu dosya `.gitignore` içindedir ve GitHub’a gönderilmez. Gerçek Wi-Fi adınızı veya şifrenizi `src/main.cpp`, `README.md` ya da başka takip edilen bir dosyaya yazmayın.

Station modunda ESP32 açılışta seri monitöre aldığı IP adresini yazar:

```text
Connected! IP: 192.168.1.xxx
```

Web arayüzünü bu IP ile açın. Bilgisayar ve ESP32 aynı yerel ağda olmalıdır.

## Web arayüzünü derleme

```bash
cd web_ui_src
npm ci
npm run build
cd ..
```

Derleme çıktısı `data/webui/` klasörüne yazılır. Arayüz değişikliği yaptıktan sonra web dosya sistemini de yüklemeniz gerekir.

## Firmware’i derleme ve yükleme

Önce kartın bilgisayardaki portunu bulun. Windows’ta örneğin `COM13` olabilir.

Sadece firmware yüklemek için:

```bash
pio run -e esp32s3 -t upload --upload-port COM13
```

`COM13` yerine kendi portunuzu yazın.

Web arayüzünü de güncellediyseniz:

```bash
pio run -e esp32s3 -t uploadfs --upload-port COM13
```

> `uploadfs` ESP32’nin dahili dosya alanını yeniden yazar. ESP32 üzerinde saklanan G-code dosyalarını silebileceği için önce yedek alın. Normal firmware yüklemesi SPIFFS dosyalarını silmez.

Seri monitör:

```bash
pio device monitor -p COM13 -b 115200
```

Seri monitörde USB cihazının bağlandığını ve IP adresini görmelisiniz.

## Yazıcıyı bağlama sırası

1. ESP32 firmware’ini yükleyin.
2. ESP32’nin USB Host/OTG portunu yazıcıya bağlayın.
3. Yazıcıyı açın.
4. ESP32’nin USB enumeration mesajını bekleyin.
5. Seri monitördeki IP adresini veya AP adresini tarayıcıda açın.

Yazıcıyı ESP32 çalışırken kapatıp açarsanız USB ve SD listesi yeniden oluşana kadar birkaç saniye bekleyin.

## G-code dosyası yükleme ve yazdırma

### ESP32 üzerinden yazdırma

1. Web arayüzünde **Upload G-code** bölümünü açın.
2. `.gcode`, `.gco` veya `.g` dosyanızı seçin.
3. Yüklemenin tamamlanmasını bekleyin.
4. Dosyanın kaynak bilgisinde `ESP32` ibaresini kontrol edin.
5. **Start Print** ile başlatın.

Bu yöntem yazıcının SD kartını kullanmaz. ESP32 dosyayı SPIFFS’ten açar ve USB üzerinden Marlin’e gönderir.

Uzun dosya adları SPIFFS yol sınırı nedeniyle kısaltılabilir. Örneğin `frc-bumper-keychain_PLA_21m49s.gcode`, arayüzde `frc-bumper-keychai.gcode` olarak görünebilir; bu aynı dosyadır.

### Yazıcı SD kartından yazdırma

1. SD kartı yazıcıya takın.
2. Yazıcının SD kartı algıladığını bekleyin.
3. **Files** ekranında SD dosyalarının görünmesini bekleyin.
4. SD dosyasını seçip **Start Print** düğmesine basın.

SD karttan dosya listeleme ve SD’den baskı başlatma Marlin’in kendi `M20`, `M23` ve `M24` komutlarını kullanır.

### ESP32 dosyasını SD karta kopyalama

Arayüz dosyayı otomatik olarak SD karta kopyalamayı deneyebilir. Bunun çalışması Marlin’in `M28` / `M29` desteğine ve SD kartın yazılabilir olmasına bağlıdır. Yazıcı `open failed` döndürürse dosya yine ESP32’de kalır ve `stream` yöntemiyle yazdırılabilir.

## Arayüz bölümleri

- **Dashboard:** baskı durumu, ilerleme ve sıcaklıklar
- **Files:** ESP32 dosyaları ve yazıcı SD dosyaları
- **Printer Control:** sıcaklık, fan, jog hareketleri ve auto home
- **Send G-code / Console:** manuel G-code gönderme ve yazıcı cevapları
- **Camera:** kamera entegrasyonu için ayrılmış alan; bu sürümde kamera bağlantısı yoktur

## REST API

| Endpoint | Method | Açıklama |
|---|---|---|
| `/api/status` | GET | Sıcaklık, konum, bağlantı ve baskı durumu |
| `/api/files` | GET | ESP32 ve yazıcı SD dosyaları |
| `/api/files/printer-sd` | GET | Yalnızca yazıcı SD dosyaları |
| `/api/files/upload` | POST | G-code’u ESP32’ye yükler, SD kopyasını deneyebilir |
| `/api/files/download?name=...` | GET | ESP32’deki dosyayı indirir |
| `/api/files/delete?name=...` | DELETE | ESP32 veya SD dosyasını siler |
| `/api/files/copy-to-printer?name=...` | POST | ESP32 dosyasını SD’ye kopyalamayı dener |
| `/api/print/start?file=...&mode=stream` | POST | ESP32 dosyasını başlatır |
| `/api/print/start?file=...&mode=sd` | POST | SD dosyasını başlatır |
| `/api/print/pause` | POST | Baskıyı duraklatır |
| `/api/print/resume` | POST | Baskıyı devam ettirir |
| `/api/print/cancel` | POST | Normal baskı durdurma |
| `/api/gcode?command=...` | POST | Ham G-code gönderir |
| `/api/move?...` | POST | Eksen hareketi |
| `/api/temperature?...` | POST | Hotend ve tabla hedefi |
| `/api/home?...` | POST | Auto home |
| `/ws` | WebSocket | Canlı durum ve ilerleme olayları |

Normal durdurma için arayüzdeki **Cancel/Stop** düğmesini kullanın. `M112` Marlin’in acil durdurma komutudur; yalnızca gerçek acil durumda kullanılmalıdır ve sonrasında yazıcının kapatılıp açılması gerekebilir.

## Sorun giderme

### ESP32’ye bağlanamıyorum

- AP modunda `Ender3-Controller` ağına bağlı olduğunuzu kontrol edin.
- Station modunda bilgisayar ve ESP32’nin aynı Wi-Fi ağında olduğunu kontrol edin.
- Seri monitörde yazan IP adresini kullanın.
- USB kablosunun veri taşıdığından emin olun.

### USB yazıcı bağlantısı yok

- ESP32-S3’ün Host/OTG portunu kullandığınızı kontrol edin.
- Yazıcıyı kapatıp açın ve birkaç saniye bekleyin.
- Seri monitörde `USB device CONNECTED` mesajını arayın.

### SD dosyaları görünmüyor

- SD kartın yazıcı tarafından tanındığını kontrol edin.
- Yazıcıyı ve ESP32’yi yeniden başlatın.
- Web sayfasında Ctrl+F5 ile önbelleği yenileyin.

### ESP dosyası başlıyor görünmesine rağmen yazıcı tepki vermiyor

- Dosyanın kaynağının `ESP32` olduğundan emin olun.
- ESP dosyası için `mode=stream`, SD dosyası için `mode=sd` kullanılmalıdır.
- Küçük bir `G28` veya `M117 TEST` dosyasıyla USB akışını test edin.
- Seri monitörde `GcodeStreamer: Started streaming` ve Marlin `ok` cevaplarını kontrol edin.

### Upload başarılı fakat SD’ye kopyalama başarısız

Bu, yazıcının SD kartı okuyabildiği ancak `M28` ile yazamadığı anlamına gelebilir. SD kartın boş alanını, FAT32 formatını ve Marlin’in SD yazma desteğini kontrol edin. Kopyalama başarısız olsa bile ESP32 dosyası silinmez; `stream` yöntemiyle kullanılabilir.

## Proje yapısı

```text
Esp32-s3D/
├── data/webui/             # Derlenmiş web arayüzü
├── include/                # Başlıklar ve yerel Wi-Fi ayarı
├── lib/                    # USB Host ve yardımcı kütüphaneler
├── src/
│   ├── main.cpp            # Wi-Fi, USB Host ve ana döngü
│   ├── web_server.cpp      # REST API, WebSocket ve dosyalar
│   ├── response_parser.h   # Marlin cevap/telemetri ayrıştırma
│   ├── gcode_streamer.*    # ESP32’den G-code stream’i
│   ├── command_scheduler.* # Komut/ACK yönetimi
│   └── printer_manager.*   # Yazıcı durum yönetimi
├── web_ui_src/             # React + TypeScript arayüz kaynağı
├── partitions.csv          # Flash bölümlendirmesi
├── platformio.ini          # PlatformIO ayarları
└── README.md
```

## Güvenlik ve katkı

- Gerçek Wi-Fi bilgilerini commit etmeyin.
- Kişisel G-code dosyalarını depoya eklemeyin.
- Web sunucusunda kullanıcı hesabı veya TLS yoktur; cihazı internete açmayın.
- Pull request göndermeden önce firmware’i `pio run -e esp32s3` ile derleyin ve web arayüzünü `npm run build` ile doğrulayın.

## Lisans

Bu proje MIT lisansı ile yayımlanmıştır.
