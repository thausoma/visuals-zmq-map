## Общая информация

Серверная часть проекта по дисциплине «Визуальное программирование».  
Клиентская часть (Android) располагается в смежном репозитории **mobiledev**.

В папке `external/` содержатся сторонние библиотеки, подключённые как субмодули:
- **Dear ImGui** + **ImPlot** — интерфейс и графики
- **SDL2**, **GLEW**, **libcurl** — окно, OpenGL-контекст и загрузка тайлов
- **libzmq** — сеть
- **libpqxx** — работа с PostgreSQL

В папке `src/` находится модульный код проекта:
- `main.cpp` — точка входа, запуск потоков
- `UI.cpp` — графический цикл (SDL2 + OpenGL3 + ImGui)
- `Map.cpp` — интерактивная карта на основе тайлов OpenStreetMap
- `Heatmap.cpp` — генерация растровых heatmap-тайлов методом IDW
- `Database.cpp` — синхронизация с PostgreSQL, миграция JSON → SQL
- `Network.cpp` — ZMQ-сервер приёма пакетов
- `Parser.cpp` — разбор входящего JSON
- `TelemetryData.cpp/h` — глобальное состояние и структуры данных

Папка `build/` внесена в `.gitignore`.

---

## Реализованный функционал

Все цели проекта достигнуты:

| Задача | Статус |
|--------|--------|
| Приём телеметрии по TCP (ZeroMQ) | Реализовано |
| Парсинг JSON и построение графиков RSRP по вышкам | Реализовано |
| Локальный бекап в `telemetry_log.json` | Реализовано |
| Интеграция PostgreSQL (хранение measurements + cell_data) | Реализовано |
| Интерактивная карта (OSM тайлы, Zoom/Pan, GPS-трек) | Реализовано |
| Heatmap (IDW-интерполяция по RSRP/RSRQ/RSSI/Altitude) | Реализовано |
| Фильтры по EARFCN и PCI для heatmap | Реализовано |
| Кастомная тёмная цветовая схема интерфейса | Реализовано |

---

## Сборка проекта

**Зависимости:** `cmake`, `g++` (C++17), `libsdl2-dev`, `libglew-dev`, `libcurl4-openssl-dev`, `libzmq3-dev`, `libpqxx-dev`, `postgresql-server-dev-all`.

```bash
mkdir build && cd build
cmake ../
make -j$(nproc)
```

Запуск:
```bash
./main        # UI + ZMQ-сервер на порту 25566
```

---

## Архитектура данных

```cpp
struct TelemetryData {
    float lat = 0, lon = 0, alt = 0, acc = 0;
    int rsrp = 0;
    std::string type = "";
    std::string raw = "";
    std::mutex mtx;

    bool db_connected = false;
    std::string data_source = "None";

    std::map<std::string, CellHistory> cell_logs;
    std::vector<double> history_lat;
    std::vector<double> history_lon;
    std::vector<double> history_time;

    double base_timestamp = 0;
    float view_min_time = 0;
    float view_max_time = 100;
    float max_recorded_time = 100;

    double heatmap_min_lat = 0, heatmap_max_lat = 0;
    double heatmap_min_lon = 0, heatmap_max_lon = 0;
    int heatmap_zoom = 15;
    bool heatmap_ready = false;
    std::string heatmap_earfcn = "";
    std::string heatmap_criterion = "";

    void clear_all() {  ...  }
};
extern TelemetryData g_data;
```

---

## Ключевые компоненты

- **`parse_json_to_data(string raw)`** — извлекает координаты, тип сети и проходит по массиву `Cells`. Для каждой вышки (LTE/GSM) формирует уникальный ID на основе PCI и частоты, после чего данные добавляются в вектор истории.

- **`zmq_server()`** — фоновый поток, принимающий JSON-пакеты по `tcp://*:25566`. Сохраняет сырые данные в файл, пишет в PostgreSQL (если доступен) и обновляет текущее состояние через парсер.

- **`sync_all_data()` / `save_packet()`** — двусторонняя синхронизация с БД. При старте загружается вся история; при приёме пакета данные уходят в SQL и JSON-бэкап одновременно. При недоступности БД работает fallback на локальный JSON.

- **`render_map_window()`** — окно карты на базе `ImPlot`. Подгружает тайлы OpenStreetMap асинхронно через `libcurl`, кэширует их как OpenGL-текстуры. Отрисовывает GPS-трек с фильтрацией по времени и текущую позицию.

- **`generate_heatmap_tiles()`** — многопоточная генерация растровых тайлов (256×256) методом **IDW** (Inverse Distance Weighting). Поддерживает критерии `RSRP`, `RSRQ`, `RSSI`, `Altitude`, настраиваемый радиус поиска и степень интерполяции. Результат накладывается поверх OSM в виде полупрозрачного слоя.

- **`ui_loop()`** — основной графический цикл. Содержит боковую панель с текущими координатами, фильтрами EARFCN/PCI, слайдерами времени, кнопкой миграции JSON → PostgreSQL и прогресс-баром генерации heatmap. Использует кастомную тёмную цветовую палитру с бирюзовыми акцентами.

---

## Примечания

- Цветовая гамма интерфейса кастомизирована и отличается от стандартной ImGui-темы (тёмно-синий фон, бирюзовые элементы управления, скруглённые рамки).
- Heatmap-генерация выполняется в фоне через `ThreadPool` (4 рабочих потока) без блокировки UI.
- Все модули (`Database`, `Map`, `Heatmap`, `Network`, `Parser`, `UI`) изолированы друг от друга и взаимодействуют через глобальную структуру `g_data` с мьютексной синхронизацией.