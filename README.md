# Ender 3 S1 ESP32-S3 Controller

Ender 3 S1 yazıcıyı ESP32-S3 üzerinden USB Host bağlantısıyla ağa açan, web tabanlı uzaktan kontrol ve izleme projesi.

Projenin hedefi; yazıcıyı BambuLab benzeri bir çalışma akışına yaklaştırmaktır:

- sıcaklık, konum, baskı durumu ve ilerleme takibi,
- yazıcının SD kartındaki G-code dosyalarını listeleme ve başlatma,
- ESP32’ye G-code yükleme,
- ESP32’de saklanan G-code’u USB üzerinden satır satır yazıcıya gönderme,
- sıcaklık, fan, eksen hareketi, auto home ve ham G-code kontrolü.

## Proje durumu

Proje aktif geliştirme aşamasındadır. Temel USB haberleşmesi, web arayüzü, telemetri, SD dosya listeleme, SD’den baskı başlatma ve ESP32 üzerinden G-code akışı çalışır.

### Dosya saklama ve baskı akışı

1. Web arayüzünden yüklenen dosya önce ESP32’nin dahili SPIFFS alanına yazılır.
2. Sistem dosyayı yazıcının SD kartına `M28` / `M29` ile aktarmayı dener.
3. Yazıcı SD’ye yazmayı kabul ederse dosya yazıcı SD’sinde görünür ve SD baskısı olarak başlatılır.
4. Yazıcı `M28` ile dosya oluşturmayı reddederse dosya ESP32’de tutulur ve arayüzden ESP32 stream baskısı olarak başlatılır.

Bu nedenle SD karttan dosya okumak çalışırken karta yazmak çalışmayabilir. Bu durumda kartın yazma koruması, dosya sistemi, boş alanı veya Marlin yapılandırması kontrol edilmelidir.

## Donanım

- Creality Ender 3 S1
- Marlin tabanlı yazıcı firmware’i
- ESP32-S3 N16R8 / 16 MB Flash / 8 MB PSRAM
- Veri destekli USB-C kablosu

ESP32-S3 üzerindeki USB-C OTG/Host portunu yazıcının USB-C portuna bağlayın. Sadece şarj destekleyen kablolar USB haberleşmesi sağlamaz.

## Yazılım mimarisi

```text
Web tarayıcısı
    │ HTTP REST + WebSocket
    ▼
ESP32-S3 firmware
    │ USB Host CDC/ACM
    ▼
Ender 3 S1 Marlin
    ├─ sıcaklık ve konum telemetrisi
    ├─ G-code komutları
    ├─ yazıcı SD dosya listesi
    └─ SD baskı kontrolü
```

### Kullanılan teknoloji

- Firmware: Arduino framework + PlatformIO
- Hedef kart: `esp32s3_n16r8`
- USB: ESP32-S3 USB Host CDC/ACM
- Ağ: ESP32 Wi-Fi AP veya Station modu
- Web sunucusu: ESPAsyncWebServer
- Gerçek zamanlı iletişim: WebSocket
- Dahili dosya alanı: ESP32 SPIFFS
- Arayüz: React 18, TypeScript, Vite, Recharts

## Kurulum

### Gereksinimler

- PlatformIO CLI veya VS Code + PlatformIO eklentisi
- Node.js ve npm
- USB veri kablosu
- ESP32-S3 kartı için uygun USB sürücüsü

### Depoyu alma

```bash
git clone https://github.com/safeapric0t/Esp32-s3D.git
cd Esp32-s3D
```

### Wi-Fi yapılandırması

Varsayılan açık kaynak yapılandırması AP modunu kullanır. ESP32 açıldığında:

```text
SSID:     Ender3-Controller
Password: ender3controller
Address:  http://192.168.4.1
```

Station moduna geçmek için [`src/main.cpp`](src/main.cpp) içindeki şu değerleri kendi ağınıza göre değiştirin:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const bool AP_MODE = false;
```

Gerçek Wi-Fi bilgilerini Git’e commit etmeyin.

### Web arayüzünü derleme

```bash
cd web_ui_src
npm ci
npm run build
cd ..
```

Derleme çıktısı `data/webui/` klasörüne yazılır ve SPIFFS imajına dahil edilir.

### Firmware’i yükleme

```bash
pio run -e esp32s3 -t upload --upload-port COM13
pio run -e esp32s3 -t uploadfs --upload-port COM13
```

`COM13` yerine bilgisayarınızdaki ESP32-S3 portunu kullanın. `uploadfs` işlemi ESP32’nin dahili dosya alanını yeniden yazar; ESP32’deki G-code dosyalarını önceden yedekleyin.

Seri monitör:

```bash
pio device monitor -p COM13 -b 115200
```

## Web arayüzü

Arayüzdeki ana bölümler:

- **Dashboard:** aktif baskı, ilerleme, sıcaklıklar, bağlantı ve hızlı yükleme
- **Files:** ESP32 dosyaları ile yazıcı SD dosyaları, silme, SD’ye kopyalama ve baskı başlatma
- **Control:** hotend/bed sıcaklığı, X/Y/Z jog hareketleri ve auto home
- **Console:** ham G-code gönderme ve yazıcı cevaplarını izleme
- **Camera:** ileride kamera entegrasyonu için ayrılmış alan
- **Settings:** arayüz ayarları ve bağlantı bilgileri

Kamera bağlantısı bu sürümde uygulanmış değildir.

## REST API

| Endpoint | Method | Açıklama |
|---|---:|---|
| `/api/status` | GET | Sıcaklık, konum, baskı ve bağlantı durumu |
| `/api/files` | GET | ESP32 ve yazıcı SD dosyalarını listeler |
| `/api/files/printer-sd` | GET | Yalnızca yazıcı SD dosyaları |
| `/api/files/upload` | POST | Multipart G-code yükleme ve otomatik SD aktarım denemesi |
| `/api/files/download?name=...` | GET | ESP32’deki G-code’u indirir |
| `/api/files/delete?name=...` | DELETE | ESP32 veya yazıcı SD dosyasını siler |
| `/api/files/copy-to-printer?name=...` | POST | ESP32 dosyasını yazıcı SD’sine kopyalamayı dener |
| `/api/print/start?file=...&mode=stream\|sd` | POST | ESP32 stream veya SD baskısı başlatır |
| `/api/print/pause` | POST | Baskıyı duraklatır |
| `/api/print/resume` | POST | Baskıyı devam ettirir |
| `/api/print/cancel` | POST | Baskıyı durdurur |
| `/api/gcode?command=...` | POST | Ham G-code gönderir |
| `/api/move?...` | POST | Göreli veya mutlak eksen hareketi |
| `/api/temperature?...` | POST | Hotend ve tabla hedef sıcaklığı |
| `/api/fan?...` | POST | Fan hızı |
| `/api/home?...` | POST | X/Y/Z auto home |
| `/ws` | WebSocket | Durum, ilerleme, log ve dosya listesi olayları |

## Güvenlik notu

Bu sürüm yerel ağ ve ESP32 erişim noktası kullanımı için tasarlanmıştır. Web sunucusunda gerçek bir kullanıcı hesabı veya TLS katmanı bulunmaz; cihazı doğrudan internete açmayın. Wi-Fi parolanızı ve ağ erişiminizi güvenli tutun.

## Proje yapısı

```text
Esp32-s3D/
├── data/webui/             # ESP32’ye yüklenecek derlenmiş arayüz
├── include/                # Ortak başlıklar
├── lib/
│   └── EspUsbHost_patched/ # USB Host kütüphanesi
├── scripts/                # Yardımcı derleme betikleri
├── src/
│   ├── main.cpp            # Wi-Fi, USB Host ve ana çalışma döngüsü
│   ├── web_server.cpp      # REST API, WebSocket ve dosya işlemleri
│   ├── response_parser.*   # Marlin cevap/telemetri ayrıştırma
│   ├── gcode_streamer.*    # ESP32’den G-code akışı
│   ├── command_scheduler.* # Komut ve ACK yönetimi
│   └── printer_manager.*   # Yazıcı durum yönetimi
├── web_ui_src/             # React + TypeScript kaynak kodu
├── partitions.csv          # Flash bölümlendirmesi
├── platformio.ini          # PlatformIO yapılandırması
└── README.md
```

## Sorun giderme

### USB yazıcı görünmüyor

- USB-C kablonun veri taşıdığını kontrol edin.
- Yazıcının USB-C portunu bilgisayarla test edin.
- ESP32-S3 üzerindeki OTG/Host portunu kullandığınızdan emin olun.
- Seri monitörde USB enumeration mesajlarını kontrol edin.

### SD dosyaları görünmüyor

- Yazıcıyı ve ESP32’yi yeniden başlatın.
- SD kartın yazıcı tarafından tanındığını kontrol edin.
- Arayüzü `Ctrl+F5` ile yenileyin.

### Upload sonrası SD’ye kopyalama başarısız

Yazıcı `M28` komutuna `open failed` döndürüyorsa SD kart okunabiliyor olsa bile yazılamıyor olabilir. Başka bir FAT32 kartla deneyin; kartı formatlamadan önce içindeki dosyaları yedekleyin.

### Baskı başlamıyor

- Dosyanın arayüzde `ESP32` veya `SD` olarak doğru işaretlendiğini kontrol edin.
- ESP32 dosyaları için `stream` yolu, yazıcı SD dosyaları için `sd` yolu kullanılır.
- Gerçek baskıdan önce `G28` gibi küçük ve güvenli bir komutla USB haberleşmesini test edin.
- Acil durum dışında normal durdurma için arayüzdeki Stop komutunu kullanın; `M112` yalnızca acil durdurma içindir.

## Katkı

Issue ve pull request göndermeden önce:

1. Gerçek Wi-Fi bilgilerini, API anahtarlarını ve kişisel G-code dosyalarını kaldırın.
2. Web arayüzünü `npm run build` ile derleyin.
3. Firmware’i `pio run -e esp32s3` ile doğrulayın.
4. Değişikliğin donanım gerektirip gerektirmediğini README’ye ekleyin.

## Lisans

Bu proje MIT lisansı altında yayımlanmıştır.
