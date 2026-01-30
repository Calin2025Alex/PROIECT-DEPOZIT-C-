// depozit.cpp
// Compile: g++ -std=c++17 depozit.cpp -o depozit

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <cstdint>
#include <limits>
#include <sstream>
#include <cctype>
#include <ctime>


// ===================== TIME / LOG UTILS =====================
std::string now() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);

    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void logAction(const std::string& msg) {
    std::ofstream log("C:\\Users\\Alex\\Desktop\\Proiect DEPOZIT\\log.txt",
                      std::ios::app);
    if (!log) {
        std::cerr << "NU POT CREA log.txt\n";
        return;
    }
    log << now() << " | " << msg << "\n";
}



// ===================== EXCEPTII =====================
class FileException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error; // moștenește constructorii lui runtime_error
};

class DataException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


// ===================== MATERIAL =====================
class Material {
    int32_t id{};
    std::string denumire;
    int32_t cantitate{};
    double pret{};

public:
    Material() = default;
    Material(int32_t i, std::string d, int32_t c, double p)
        : id(i), denumire(std::move(d)), cantitate(c), pret(p) {}

    int32_t getId() const { return id; }
    const std::string& getName() const { return denumire; }
    int32_t getQty() const { return cantitate; }
    double getPrice() const { return pret; }

    void setQty(int32_t q) {
        if (q < 0) throw DataException("Cantitate negativa");
        cantitate = q;
    }

    double value() const { return cantitate * pret; }

    // --- SERIALIZARE BINARA ---
    void serialize(std::ostream& os) const {
    uint32_t len = static_cast<uint32_t>(denumire.size());

    os.write(reinterpret_cast<const char*>(&id), sizeof(id));
    os.write(reinterpret_cast<const char*>(&len), sizeof(len));
    os.write(denumire.data(), len);
    os.write(reinterpret_cast<const char*>(&cantitate), sizeof(cantitate));
    os.write(reinterpret_cast<const char*>(&pret), sizeof(pret));

    if (!os) throw FileException("Eroare la scriere fisier");
}


    static Material deserialize(std::istream& is) {
        int32_t id;
        int32_t qty;
        double price;
        uint32_t len;

        if (!is.read((char*)&id, sizeof(id))) throw FileException("EOF");
        is.read((char*)&len, sizeof(len));
        std::string name(len, '\0');
        is.read(&name[0], len);
        is.read((char*)&qty, sizeof(qty));
        is.read((char*)&price, sizeof(price));

        return {id, name, qty, price};
    }

    std::string toString() const {
        std::ostringstream ss;
        ss << std::left << std::setw(6) << id
           << std::setw(30) << denumire
           << std::right << std::setw(8) << cantitate
           << std::setw(12) << std::fixed << std::setprecision(2) << pret
           << std::setw(14) << value();
        return ss.str();
    }
};

// ===================== DEPOZIT =====================
class Depozit {
    std::vector<Material> items;
    std::string filename;
    std::vector<std::vector<Material>> undoStack;
    std::vector<std::vector<Material>> redoStack;

public:
    explicit Depozit(std::string f) : filename(std::move(f)) 
    {
        logAction("DEPOZIT initializat, fisier=" + filename);
    }

    // ================= ITERATOR CUSTOM =================
    class Iterator {
        const std::vector<Material>* v;
        size_t i;
    public:
        Iterator(const std::vector<Material>* vec, size_t idx) : v(vec), i(idx) {}
        const Material& operator*() const { return (*v)[i]; }
        Iterator& operator++() { ++i; return *this; }
        bool operator!=(const Iterator& other) const { return i != other.i; }
    };

    Iterator begin() const { return {&items, 0}; }
    Iterator end() const { return {&items, items.size()}; }

    // ================= CRUD =================
    void add(const Material& m) 
    {
        if (exists(m.getId()))
            throw DataException("ID deja existent");

        saveState(); // 👈 FOARTE IMPORTANT
        items.push_back(m);

        logAction("ADD ID=" + std::to_string(m.getId()) +
          " NAME=" + m.getName() +
          " QTY=" + std::to_string(m.getQty()) +
          " PRICE=" + std::to_string(m.getPrice()));

    }


    void remove(int id)    
    {
        auto it = std::remove_if(items.begin(), items.end(),
            [id](const Material& m){ return m.getId() == id; });

        if (it == items.end())
            throw DataException("ID inexistent");

        saveState(); 
        items.erase(it, items.end());

        logAction("DELETE material ID=" + std::to_string(id));
    }


    // ================= UNDO / REDO =================
    bool undo() 
    {
        if (undoStack.empty())
            return false;

        redoStack.push_back(items);
        items = undoStack.back();
        undoStack.pop_back();

        logAction("UNDO");
        return true;
    }

    bool redo() 
    {
        if (redoStack.empty())
            return false;

        undoStack.push_back(items);
        items = redoStack.back();
        redoStack.pop_back();

        logAction("REDO");
        return true;
    }



    void updateQty(int id, int q) {
        for (auto& m : items)
            if (m.getId() == id) {
                saveState();
                m.setQty(q);
                logAction("UPDATE QTY ID=" + std::to_string(id) +
                          " NEW_QTY=" + std::to_string(q));
                return;
            }
        throw DataException("ID inexistent");
    }

    bool exists(int id) const {
        return std::any_of(items.begin(), items.end(),
            [id](const Material& m){ return m.getId() == id; });
    }

    // ================= FILTRE =================
    std::vector<Material> lowStock(int t = 5) const {
        std::vector<Material> r;
        for (const auto& m : items)
            if (m.getQty() <= t) r.push_back(m);
        logAction("FILTER lowStock <= " + std::to_string(t));    
        return r;
    }

    std::vector<Material> outOfStock() const {
        std::vector<Material> r;
        for (const auto& m : items)
            if (m.getQty() == 0) r.push_back(m);
        logAction("FILTER outOfStock");
        return r;
    }

    std::vector<Material> expensive(double p = 10000) const {
        std::vector<Material> r;
        for (const auto& m : items)
            if (m.getPrice() > p) r.push_back(m);
        logAction("FILTER expensive > " + std::to_string(p));
        return r;
    }

    // ================= CAUTARE =================
    std::vector<Material> search(std::string q) const {
        std::vector<Material> r;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);

        for (const auto& m : items) {
            std::string d = m.getName();
            std::transform(d.begin(), d.end(), d.begin(), ::tolower);
            if (d.find(q) != std::string::npos)
                r.push_back(m);
        }
        logAction("SEARCH query=\"" + q + "\"");
        return r;
    }

    // ================= SORTARI =================
    std::vector<Material> sortByPrice(bool asc = true) const {
        auto r = items;
        std::sort(r.begin(), r.end(),
            [asc](const Material& a, const Material& b) {
                return asc ? a.getPrice() < b.getPrice()
                           : a.getPrice() > b.getPrice();
            });
        logAction(std::string("SORT price ") + (asc ? "ASC" : "DESC"));
        return r;
    }

    std::vector<Material> sortByQuantity(bool asc = true) const {
        auto r = items;
        std::sort(r.begin(), r.end(),
            [asc](const Material& a, const Material& b) {
                return asc ? a.getQty() < b.getQty()
                           : a.getQty() > b.getQty();
            });
        logAction(std::string("SORT quantity ") + (asc ? "ASC" : "DESC"));
        return r;
    }

    // ================= STATISTICI =================
    double totalValue() const {
        double s = 0;
        for (const auto& m : items) s += m.value();
        logAction("STATS totalValue=" + std::to_string(s));
        return s;
    }

    // ================= FILE BINAR =================
    void load() {
        items.clear();
        std::ifstream f(filename, std::ios::binary);
        if (!f) 
        {
            logAction("LOAD failed (file missing)");
            return;
        }
        while (f.peek() != EOF)
            items.push_back(Material::deserialize(f));

        undoStack.clear();
        redoStack.clear();
        saveState();   // ← salvează starea inițială

        logAction("LOAD from file=" + filename);
    }

    void save() const {
        std::ofstream f(filename, std::ios::binary);
        if (!f) throw FileException("Nu pot salva fisierul");
        for (const auto& m : items)
            m.serialize(f);

        logAction("SAVE to file=" + filename);
    }

    // ================= EXPORT =================
    void exportCSV(const std::string& outFile) const {
        std::ofstream f(outFile);
        if (!f) throw FileException("Nu pot crea fisier CSV");

        f << "ID,Denumire,Cantitate,Pret,Valoare\n";
        for (const auto& m : items) {
            f << m.getId() << ","
              << m.getName() << ","
              << m.getQty() << ","
              << m.getPrice() << ","
              << m.value() << "\n";
        }
        logAction("EXPORT CSV file=" + outFile);
    }

    // ================= IMPORT CSV =================
void importCSV(const std::string& inFile, bool replace = false) {
    std::ifstream f(inFile);
    if (!f) throw FileException("Nu pot deschide fisier CSV");

    saveState(); 
    if (replace)
        items.clear();

    std::string line;
    std::getline(f, line); // sari peste header

    while (std::getline(f, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string field;

        int id, cant;
        double pret;
        std::string nume;

        // ID
        std::getline(ss, field, ',');
        id = std::stoi(field);

        // Denumire
        std::getline(ss, nume, ',');

        // Cantitate
        std::getline(ss, field, ',');
        cant = std::stoi(field);

        // Pret
        std::getline(ss, field, ',');
        pret = std::stod(field);

        if (exists(id))
            throw DataException("ID duplicat in CSV: " + std::to_string(id));

        items.emplace_back(id, nume, cant, pret);
    }
    logAction("IMPORT CSV file=" + inFile +
                  (replace ? " REPLACE" : " APPEND"));
}

    void exportTXT(const std::string& outFile) const {
        std::ofstream f(outFile);
        if (!f) throw FileException("Nu pot crea fisier TXT");

        f << std::left << std::setw(6) << "ID"
          << std::setw(30) << "Denumire"
          << std::right << std::setw(8) << "Cant"
          << std::setw(12) << "Pret"
          << std::setw(14) << "Valoare\n";
        f << std::string(70,'-') << "\n";

        for (const auto& m : items)
            f << m.toString() << "\n";

        logAction("EXPORT TXT file=" + outFile);
    }

    // ================= DEMO =================
    void demo() 
    {
        add({1,"Surub M6",120,0.15});
        add({2,"Motor 5kW",2,12500});
        add({3,"Cablu YKY",4,12.5});
        add({4,"Invertor",1,22000});

        undoStack.clear();
        redoStack.clear();
        saveState();  

        logAction("DEMO data loaded");
    }

    void saveState() 
    {
        undoStack.push_back(items);
        if (undoStack.size() > 20)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }


    // ================= ACCESS =================
    const std::vector<Material>& all() const { return items; }
};


// ===================== UI =====================
void print(const std::vector<Material>& v) {
    std::cout << std::left << std::setw(6) << "ID"
              << std::setw(30) << "Denumire"
              << std::right << std::setw(8) << "Cant"
              << std::setw(12) << "Pret"
              << std::setw(14) << "Valoare\n";
    std::cout << std::string(70,'-') << "\n";
    for (const auto& m : v)
        std::cout << m.toString() << "\n";
}

void printPaged(const std::vector<Material>& v, size_t pageSize = 10) {
    if (v.empty()) {
        std::cout << "Nu exista elemente de afisat.\n";
        return;
    }

    size_t totalPages = (v.size() + pageSize - 1) / pageSize;
    size_t page = 0;
    char cmd;

    do {
        system("cls"); // pe Linux: system("clear");

        std::cout << "Pagina " << (page + 1) << " / " << totalPages << "\n\n";

        std::cout << std::left << std::setw(6) << "ID"
                  << std::setw(30) << "Denumire"
                  << std::right << std::setw(8) << "Cant"
                  << std::setw(12) << "Pret"
                  << std::setw(14) << "Valoare\n";
        std::cout << std::string(70,'-') << "\n";

        size_t start = page * pageSize;
        size_t end = std::min(start + pageSize, v.size());

        for (size_t i = start; i < end; ++i)
            std::cout << v[i].toString() << "\n";

        std::cout << "\n[n] Next  [p] Prev  [q] Quit : ";
        std::cin >> cmd;

        if (cmd == 'n' && page + 1 < totalPages) page++;
        if (cmd == 'p' && page > 0) page--;

    } while (cmd != 'q');
}

int readInt(const std::string& msg) {
    int x;
    while (true) {
        std::cout << msg;
        if (std::cin >> x) return x;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Input invalid!\n";
    }
}

void showMenu() {
    std::cout << "\n======= Meniu Depozit =======\n"
              << "---------------------------------------------\n"
              << "1. Afisare inventar complet\n"
              << "2. Afisare materiale cu cantitate <= 4\n"
              << "3. Afisare materiale epuizate (cant=0)\n"
              << "4. Afisare materiale foarte scumpe (pret>10000)\n"
              << "---------------------------------------------\n"
              << "5. Adauga material\n"
              << "7. Modifica Cantitatea unui Material\n"
              << "8. Sterge Material dupa ID\n"
              << "9. Cauta Material dupa Denumire\n"
              << "---------------------------------------------\n"
              << "6. Salvare si iesire\n"
              << "0. Iesire fara salvare\n"
              << "---------------------------------------------\n"
              << "10. Sortare dupa pret (crescator)\n"
              << "11. Sortare dupa pret (descrescator)\n"
              << "12. Sortare dupa cantitate (crescator)\n"
              << "13. Sortare dupa cantitate (descrescator)\n"
              << "---------------------------------------------\n"
              << "14. Export inventar in CSV\n"
              << "15. Export inventar in TXT\n"
              << "---------------------------------------------\n"
              << "16. Import inventar din CSV\n"
              << "---------------------------------------------\n"
              << "17. Undo ultima operatie\n"
              << "18. Redo operatie\n"
              << "---------------------------------------------\n"
              << "Alege optiunea: ";
}


// ===================== MAIN =====================
int main() {
    Depozit d("depozit.dat");
    logAction("Aplicatie pornita");

    try {
        d.load();
        if (d.all().empty())
            d.demo();
    } catch (const std::exception& e) {
        std::cout << "Eroare la incarcare: " << e.what() << "\n";
        logAction(std::string("ERROR: ") + e.what());
        d.demo();
    }

    bool ruleaza = true;
    while (ruleaza) { 
    showMenu();

    int opt;
    std::cin >> opt;

    try {
        switch (opt) {

        case 1: // Afisare completa
            printPaged(d.all());
            break;

        case 2: // Cantitate <= 4
            printPaged(d.lowStock(4));
            break;

        case 3: // Epuizate
            printPaged(d.outOfStock());
            break;

        case 4: // Scumpe
            printPaged(d.expensive(10000));
            break;

        case 5: { // Adauga
            int id = readInt("ID: ");
            std::cin.ignore();
            std::string nume;
            std::cout << "Denumire: ";
            std::getline(std::cin, nume);
            int cant = readInt("Cantitate: ");
            double pret;
            std::cout << "Pret unitar: ";
            std::cin >> pret;

            d.add({id, nume, cant, pret});
            std::cout << "Material adaugat cu succes!\n";
            break;
        }

        case 7: // Modifica cantitate
            d.updateQty(
                readInt("ID material: "),
                readInt("Noua cantitate: ")
            );
            std::cout << "Cantitate modificata.\n";
            break;

        case 8: // Stergere
            d.remove(readInt("ID material: "));
            std::cout << "Material sters.\n";
            break;

        case 9: { // Cautare
            std::cin.ignore();
            std::string q;
            std::cout << "Text cautat: ";
            std::getline(std::cin, q);
            printPaged(d.search(q));
            break;
        }

        case 10: // Sortare pret crescator
            printPaged(d.sortByPrice(true));
            break;

        case 11: // Sortare pret descrescator
            printPaged(d.sortByPrice(false));
            break;

        case 12: // Sortare cantitate crescator
            printPaged(d.sortByQuantity(true));
            break;

        case 13: // Sortare cantitate descrescator
            printPaged(d.sortByQuantity(false));
            break;

        case 14: // Export CSV
            d.exportCSV("inventar.csv");
            std::cout << "Export CSV realizat: inventar.csv\n";
            break;

        case 15: // Export TXT
            d.exportTXT("inventar.txt");
            std::cout << "Export TXT realizat: inventar.txt\n";
            break;

        case 16: { // Import CSV
            std::cin.ignore();
            std::string file;
            std::cout << "Fisier CSV: ";
            std::getline(std::cin, file);

            std::cout << "Inlocuieste datele existente? (1=DA, 0=NU): ";
            int r;
            std::cin >> r;

            d.importCSV(file, r == 1);
            std::cout << "Import CSV realizat cu succes.\n";
            break;
        }

        case 17:
            if (d.undo())
                std::cout << "Undo realizat.\n";
            else
                std::cout << "Nu exista undo.\n";
            break;

        case 18:
            if (d.redo())
                std::cout << "Redo realizat.\n";
            else
                std::cout << "Nu exista redo.\n";
            break;


        case 6: // Salvare si iesire
            d.save();
            std::cout << "Date salvate.\n";
            ruleaza = false;
            break;

        case 0: // Iesire fara salvare
            std::cout << "Iesire fara salvare.\n";
            ruleaza = false;
            break;

        default:
            std::cout << "Optiune invalida!\n";
        }

    } catch (const std::exception& e) {
        std::cout << "Eroare: " << e.what() << "\n";
    }
}
logAction("PROGRAM STOP");
    return 0;
}