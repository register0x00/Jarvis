#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <windows.h>
#include <conio.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

std::map<std::string, std::string> triggerMap;
bool usp;
int8_t count;
// ---------- Вспомогательные функции ----------

// Конвертирует строку из системной кодировки (ANSI/CP1251) в UTF-8
// Работает без std::locale — максимально надёжно
std::string ansiToUtf8(const std::string& s) {
    UINT cp = GetACP(); // Получаем системную кодировку (обычно 1251 на русской Windows)
    
    // Сначала в wchar_t
    int wlen = MultiByteToWideChar(cp, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), -1, &w[0], wlen);

    // Потом в UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string res(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &res[0], len, nullptr, nullptr);

    if (!res.empty()) res.pop_back(); // Убираем завершающий \0
    return res;
}

// Обратно: UTF-8 → системная кодировка (для system())
std::string utf8ToAnsi(const std::string& s) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], wlen);

    UINT acp = GetACP();
    int len = WideCharToMultiByte(acp, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string res(len, '\0');
    WideCharToMultiByte(acp, 0, w.c_str(), -1, &res[0], len, nullptr, nullptr);

    if (!res.empty()) res.pop_back();
    return res;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> splitIntoWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == ',' || c == ';') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

std::string cleanUp(std::string s) {
    if (!s.empty() && s.back() == '.') s.pop_back();
    size_t first = s.find_first_not_of(' ');
    size_t last = s.find_last_not_of(' ');
    if (first == std::string::npos) return "";
    return s.substr(first, (last - first + 1));
}

bool parseTriggerLine(const std::string& line, std::string& trigger, std::string& command) {
    // Ищем разделитель '!'
    size_t sepPos = line.find('!');
    if (sepPos == std::string::npos) return false; // Нет '!', строка невалидна

    // Ищем терминатор ';'
    size_t termPos = line.find(';');
    if (termPos == std::string::npos || termPos < sepPos) return false; // Нет ';' или он до '!'

    trigger = cleanUp(line.substr(0, sepPos));       // Всё до '!'
    command = cleanUp(line.substr(sepPos + 1, termPos - sepPos - 1)); // Между '!' и ';'

    return !trigger.empty() && !command.empty();
}

struct Remind {
    int totalMinutes;
    std::string text; // храним как есть (ANSI байты)
    bool fired;
};

// ---------- Главная программа ----------

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "=========Текстовый помощник J.A.R.V.I.S. beta.3 - запущен=========\n\n";

    // 1. Читаем события из файла
    std::vector<Remind> reminders;
    std::ifstream file("events.txt");
    if (!file.is_open()) {
        std::cerr << "Файл events.txt не найден. Будем работать без напоминаний.\n";
    } else {
        std::string line;
        while (std::getline(file, line)) {
            // Убираем возможный CR (\r) если файл из другой ОС
            if (!line.empty() && line.back() == '\r') line.pop_back();
            
            if (line.empty()) continue;
            if (line.length() < 4) {
                std::cerr << "Пропущена слишком короткая строка: " << line << "\n";
                continue;
            }

            int timeVal = 0;
            try {
                // Первые 4 символа должны быть цифрами
                timeVal = std::stoi(line.substr(0, 4));
            } catch (...) {
                std::cerr << "Пропущена строка с некорректным временем: " << line << "\n";
                continue;
            }

            // Ищем первый непробельный символ после 4-х цифр
            size_t textStart = line.find_first_not_of(' ', 4);
            if (textStart == std::string::npos) {
                std::cerr << "Пропущена строка: нет текста после времени (" << line << ")\n";
                continue;
            }
            std::string eventText = line.substr(textStart);

            int h = timeVal / 100;
            int m = timeVal % 100;

            if (h >= 0 && h < 24 && m >= 0 && m < 60) {
                reminders.push_back({h * 60 + m, eventText, false});
            } else {
                std::cerr << "Пропущена строка с некорректным временем: " << line << "\n";
            }
        }
        file.close();
        std::cout << "Загружено напоминаний: " << reminders.size() << "\n";
    }

    std::ifstream trigFile("triggers.txt");
if (trigFile.is_open()) {
    std::string line;
    int loadedCount = 0;
    while (std::getline(trigFile, line)) {
        // Убираем возможный CR (\r) если файл из другой ОС
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        if (line.empty()) continue; // Пропускаем пустые строки

        std::string tr, cmd;
        if (parseTriggerLine(line, tr, cmd)) {
            // Сохраняем в map. toLower нужен, чтобы регистр не мешал
            triggerMap[toLower(tr)] = cmd; 
            loadedCount++;
        } else {
            std::cerr << "[Память] Пропущена невалидная строка: " << line << "\n";
        }
    }
    trigFile.close();
    std::cout << "Загружено правил из памяти: " << loadedCount << "\n";
} else {
    // Если файла нет — это не ошибка, просто нет правил
    std::cout << "[Память] Файл triggers.txt не найден. Память пуста.\n";
}

    auto lastSigh = std::chrono::system_clock::now();
    const int SIGH_INTERVAL_SEC = 30;


    // 2. Главный цикл
    while (true) {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&timeT);
        int nowTotalMinutes = tm.tm_hour * 60 + tm.tm_min;
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - lastSigh).count();
        if (diff >= SIGH_INTERVAL_SEC) {
            std::cout << "*вздыхает* Вы ещё тут? Если не знаете с чего начать введите help или помощь\n";
            lastSigh = now;
            std::cout << "> ";
            count += 1;
        }
        if (count == 2){
            std::cout << "Сэр, мне скоро придётся отключиться, чтобы не тратить ресурсы процессора, пожалуйста напишите команду.\n";
            std::cout << "> ";
        }
        if (count == 4){
            return 0;
        }

        for (auto &r : reminders) {
            if (r.fired) continue;
            if (nowTotalMinutes >= r.totalMinutes) {
                // Конвертируем текст в UTF-8 только при выводе
                std::cout << "\n НАПОМИНАНИЕ: " << ansiToUtf8(r.text) << "\n> ";
                r.fired = true;
            }
        }

        if (_kbhit()) {
            usp = false;
            count = 0;
            std::string line;
            if (std::getline(std::cin, line)) {
                line = ansiToUtf8(line); // Ввод тоже конвертируем

                std::vector<std::string> words = splitIntoWords(line);

                if (words.empty()) continue; 

                bool hasPlease = false;
                std::string action = "";
                std::string payload = "";

                for (size_t i = 0; i < words.size(); ++i) {
                    std::string w = toLower(words[i]);

                    if (w == "пожалуйста") {
                        hasPlease = true;
                        continue;
                    }
                    if (w == "спасибо") {
                        std::cout << "Не за что, сэр\n> ";
                        usp = true;
                        continue;
                    }

                    // ПРОВЕРКА ПАМЯТИ
                    auto it = triggerMap.find(w);
                    if (it != triggerMap.end()) {
                        // Мы нашли триггер! Теперь нужно выполнить ту команду, которую ты прописал.
                        std::string realCommand = it->second; // Например, "!запусти"

                        // Убираем префикс '!' если он есть, чтобы дальше сработала обычная логика
                        if (!realCommand.empty() && realCommand[0] == '!') {
                            realCommand = realCommand.substr(1);
                        }

                        // Подменяем текущее слово w на реальную команду и продолжаем цикл.
                        // Программа дальше пойдёт по обычным проверкам: if (w == "запусти")...
                        w = realCommand; 
                        // Важно: мы НЕ делаем continue, мы просто меняем значение w,
                        // чтобы сработали обычные условия ниже.
                    }

                    if (w == "привет" || w == "дарово" || w == "здравствуй") {
                        std::cout << "Здравствуйте, сэр\n> ";
                        usp = true;
                        continue;
                    }
                    if (w == "help" || w == "помощь") {
                        system("start ""Jarvis_help.htm""");
                        usp = true;
                        std::cout << "> ";
                        continue;
                    }
                    if (w == "пока" || w == "до" || w == "эль") {
                        std::cout << "До свидания, сэр\n";
                        return 0;
                    }
                    if (w == "открой" || w == "запусти") {
                        action = "запусти";
                        usp = true;
                        for (size_t j = i + 1; j < words.size(); ++j) {
                            if (!payload.empty()) payload += " ";
                            payload += words[j];
                        }
                        continue;
                    }
                    if (w == "найди" || w == "вбей") {
                        action = "найди";
                        usp = true;
                        for (size_t j = i + 1; j < words.size(); ++j) {
                            if (!payload.empty()) payload += " ";
                            payload += words[j];
                        }
                        continue;
                    }
                    if (usp == false) {
                        std::cout << "Не известная команда, скорее всего технолайз не притёрся\n";
                        std::cout << "> ";
                        continue;
                    }
                } 
                if (action.empty()) continue;

                payload = cleanUp(payload);
                if (payload.empty()) {
                    if (action == "запусти")
                        std::cout << "Напишите, что Вы хотели запустить\n> ";
                    else
                        std::cout << "Напишите, что Вы хотели найти\n> ";
                    continue;
                }

                if (action == "запусти") {
                    std::string command1 = "start \"\" \"" + utf8ToAnsi(payload) + "\"";
                    if (hasPlease)
                        std::cout << "С удовольствием, сэр. Запускаю: " << payload << "\n> ";
                    else
                        std::cout << "Запускаю: " << payload << "\n> ";
                    system(command1.c_str());
                } else if (action == "найди") {
                    const char* hex = "0123456789ABCDEF";
                    std::string sp;
                    for (unsigned char c : payload) {
                        if (c == ' ') sp += "%20";
                        else if (c > 127) {
                            sp += '%';
                            sp += hex[c / 16];
                            sp += hex[c % 16];
                        } else {
                            sp += (char)c;
                        }
                    }
                    std::string command = "start \"\" \"https://yandex.ru/search/?text=" + sp + "\"";
                    if (hasPlease) {
                        std::cout << "С удовольствием, сэр! ";
                        std::cout << "Помните где бы Вы ни были все соединены!\n";
                        std::cout << "> ";
                        continue;
                    }else{
                    std::cout << "Ищу: " << payload << "\n> ";
                    system(command.c_str());
                    continue;
                }
            }
         } 
         } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    }
}

