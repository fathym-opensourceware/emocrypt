#include <cassert>
#include <cerrno>
#include <common/encrypt.h>
#include <common/fd.h>
#include <common/format.h>
#include <common/options.h>
#include <common/term_echo.h>
#include <common/version.h>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string.h>
#include <termios.h>
#include <unistd.h>

using std::unique_ptr;
using std::make_unique;

namespace {

    std::string get_password(const std::string& prompt)
    {
        ec::fprint(std::cerr, prompt);
        std::cout.flush();

        std::string password;
        char ch;
        ec::FD tty("/dev/tty", O_RDONLY);

        ec::TermEcho term_echo(tty.fd());
        term_echo.disable();

        while(true) {
            auto ret = tty.read(&ch, sizeof(ch));
            if(!ret || ch == '\n')
                break;
            
            password.append(1, ch);
        }

        ec::fprint(std::cerr, "\n");
        assert(password.find('\n') == std::string::npos);
        
        return password;
    }

    bool decrypt(const std::string& infile, const std::string& outfile)
    {
        std::string password = get_password("Password: ");
        if(password.empty()) {
            ec::fprintln(std::cerr, "Password is required");
            return false;
        }
        
        std::ifstream fis;
        std::istream* is = &std::cin;
        if(infile.size()) {
            fis.open(infile, std::ios::in|std::ios::binary);
            if(!fis) {
                ec::fprintln(std::cerr, "open(\"", infile, "\"): ", strerror(errno));
                return false;
            }
            fis.exceptions(std::ios::badbit);
            is = &fis;
        }

        ec::Symbols symbols = ec::load_symbols();
        std::string encoded_ciphertext((std::istreambuf_iterator<char>(*is)),
                                        std::istreambuf_iterator<char>());
        ec::byte_string ciphertext = ec::decode(symbols, encoded_ciphertext);
        if(ciphertext.empty()) {
            ec::fprintln(std::cerr, "Invalid ciphertext data");
            return false;
        }

        ec::byte_string plaintext = ec::decrypt(ciphertext.data(), ciphertext.size(), password);
        if(plaintext.empty()) {
            ec::fprintln(std::cerr, "Decryption failed");
            return false;
        }

        std::ofstream fos;
        std::ostream* os = &std::cout;
        if(outfile.size()) {
            fos.open(outfile, std::ios::out|std::ios::binary|std::ios::trunc);
            if(!fos) {
                ec::fprintln(std::cerr, "open(\"", outfile, "\"): ", strerror(errno));
                return false;
            }
            fos.exceptions(std::ios::badbit);
            os = &fos;
        }

        plaintext.append('\n', 1);
        os->write(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
        return true;
    }

    bool encrypt(const std::string& infile, const std::string& outfile, int line_length)
    {
        std::string password = get_password("Password: ");
        if(password.empty()) {
            ec::fprintln(std::cerr, "Password is required");
            return false;
        }

        std::string confirmation = get_password("Confirmation: ");
        if(confirmation != password) {
            ec::fprintln(std::cerr, "Password and confirmation do not match");
            return false;
        }

        std::ifstream fis;
        std::istream* is = &std::cin;
        if(infile.size()) {
            fis.open(infile, std::ios::binary|std::ios::in);
            if(!fis) {
                ec::fprintln(std::cerr, "open(\"", infile, "\"): ", strerror(errno));
                return false;
            }
            fis.exceptions(std::ios::badbit);
            is = &fis;
        }

        std::string plaintext((std::istreambuf_iterator<char>(*is)),
                              std::istreambuf_iterator<char>());
        if(is->bad()) {
            ec::fprintln(std::cerr, "read(): ", strerror(errno));
            return false;
        }

        ec::byte_string ciphertext = ec::encrypt(plaintext.data(), plaintext.size(), password);
        if(ciphertext.empty()) {
            ec::fprintln(std::cerr, "Encryption failed");
            return false;
        }

        std::random_device rd;
        std::mt19937 rng(rd());
        ec::Symbols symbols = ec::load_symbols();
        std::string encoded_ciphertext = ec::encode(rng, symbols, ciphertext.data(), ciphertext.size(), line_length);
        
        std::ofstream fos;
        std::ostream* os = &std::cout;
        if(outfile.size()) {
            fos.open(outfile, std::ios::binary|std::ios::out|std::ios::trunc);
            if(!fos) {
                ec::fprintln(std::cerr, "open(\"", outfile, "\"): ", strerror(errno));
                return false;
            }
            fos.exceptions(std::ios::badbit);
            os = &fos;
        }
        os->write(encoded_ciphertext.data(), encoded_ciphertext.size());
        return true;
    }

}

int main(int argc, char** argv)
{
    ec::Options opt;
    opt.add("infile", ec::ArgType::Required, 'i');
    opt.add("outfile", ec::ArgType::Required, 'o');
    opt.add("decrypt", ec::ArgType::None, 'd');
    opt.add("encrypt", ec::ArgType::None, 'e');
    opt.add("line-length", ec::ArgType::Required, 'l');
    opt.add("version", ec::ArgType::None, 'v');
    opt.add("help", ec::ArgType::None, 'h');
    opt.parse(argc, argv);

    std::string infile;
    if(opt.isPresent("infile"))
        infile = opt.arg("infile");
    
    std::string outfile;
    if(opt.isPresent("outfile"))
        outfile = opt.arg("outfile");

    int line_length = 20;
    if(opt.isPresent("line-length"))
        line_length = std::stoi(opt.arg("line-length"));

    if(opt.isPresent("help")) {
        ec::fprintln(std::cerr, "Usage: ", argv[0], " <options>");
        ec::fprintln(std::cerr, "Options:");
        ec::fprintln(std::cerr, "  --infile,-i        Input file (default: stdin)");
        ec::fprintln(std::cerr, "  --outfile,-o       Output file (default: stdout)");
        ec::fprintln(std::cerr, "  --decrypt,-d       Decrypt");
        ec::fprintln(std::cerr, "  --encrypt,-e       Encrypt (default)");
        ec::fprintln(std::cerr, "  --line-length,-l   Line length (default: 80)");
        ec::fprintln(std::cerr, "  --version,-v       Show program version");
        ec::fprintln(std::cerr, "  --help,-h          Show help");
        return 0;
    } else if(opt.isPresent("version")) {
        ec::println(argv[0], " ", ec::VERSION_MAJOR, ".", ec::VERSION_MINOR);
    } else if(opt.isPresent("decrypt")) {
        return !decrypt(infile, outfile);
    } else {
        return !encrypt(infile, outfile, line_length);
    }
    return 0;
}
