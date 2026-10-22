#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace ftxui;
using namespace std;

// ============================================================================
// CONFIGURAÇÃO
// ============================================================================
namespace config {
constexpr int ESTOQUE_MINIMO = 20;
constexpr int LIMITE_QUANTIDADE = 1000000;
constexpr int ANO_MINIMO = 1900;
constexpr int ANO_MAXIMO = 2100;

const string ARQ_MAQUINAS = "maquinas.txt";
const string ARQ_PECAS = "pecas.txt";
const string ARQ_ESTOQUE = "estoque.txt";
const string ARQ_RELACOES = "relacionamentos_maquina_peca.txt";
const string ARQ_VENDAS = "vendas_semana.txt";
const string ARQ_MANUTENCOES = "manutencoes.txt";
}  // namespace config

// ============================================================================
// 1. MODELO
// ============================================================================
enum class SituacaoEstoque { OK, CRITICO, SEM_ESTOQUE };
enum class StatusMaquina { FUNCIONANDO, EM_MANUTENCAO, PARADA, INATIVA };

const vector<string>& nomesStatusMaquina() {
    static const vector<string> nomes = {
        "FUNCIONANDO", "EM MANUTENÇÃO", "PARADA", "INATIVA"
    };
    return nomes;
}

string nomeStatus(StatusMaquina status) {
    const size_t indice = static_cast<size_t>(status);
    if (indice >= nomesStatusMaquina().size()) return "INATIVA";
    return nomesStatusMaquina()[indice];
}

string nomeSituacao(SituacaoEstoque situacao) {
    switch (situacao) {
        case SituacaoEstoque::OK: return "OK";
        case SituacaoEstoque::CRITICO: return "CRÍTICO";
        case SituacaoEstoque::SEM_ESTOQUE: return "SEM ESTOQUE";
    }
    return "?";
}

struct Maquina {
    int id = 0;               // identificador interno, não exibido ao usuário
    string nome;
};

struct Peca {
    int id = 0;
    string nome;
};

struct ItemEstoque {
    int idPeca = 0;
    int quantidade = 0;
};

// Relação: Máquina -> Peça -> Quantidade necessária.
// NÃO representa estoque dentro da máquina.
struct MaquinaPeca {
    int idMaquina = 0;
    int idPeca = 0;
    int quantidadeNecessaria = 1;
};

struct Venda {
    int idMaquina = 0;
    int quantidade = 0;  // quantidade de máquinas vendidas nos registros novos
    string cliente;
    string nomeMaquina;  // snapshot para manter o histórico legível
    bool registroAntigoDePecas = false;  // mantém o histórico antigo sem contá-lo como máquinas
};

struct PecaUsada {
    int idPeca = 0;
    int quantidade = 0;
};

struct Manutencao {
    int id = 0;
    int idMaquina = 0;
    string data;
    string observacao;
    vector<PecaUsada> pecas;
};

struct Resultado {
    bool ok = false;
    string mensagem;

    static Resultado Sucesso(string texto = "") {
        return {true, std::move(texto)};
    }

    static Resultado Falha(string texto) {
        return {false, std::move(texto)};
    }
};

struct BancoDados {
    vector<Maquina> maquinas;
    vector<Peca> pecas;
    vector<ItemEstoque> estoque;
    vector<MaquinaPeca> relacoes;
    vector<Venda> vendas;
    vector<Manutencao> manutencoes;
    bool alterado = false;

    void marcarAlteracao() { alterado = true; }
};

// ============================================================================
// UTILIDADES
// ============================================================================
string aparar(const string& texto) {
    const string espacos = " \t\r\n";
    const size_t inicio = texto.find_first_not_of(espacos);
    if (inicio == string::npos) return "";
    const size_t fim = texto.find_last_not_of(espacos);
    return texto.substr(inicio, fim - inicio + 1);
}

string minusculas(string texto) {
    for (char& c : texto)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return texto;
}

bool mesmoTexto(const string& a, const string& b) {
    return minusculas(aparar(a)) == minusculas(aparar(b));
}

string limparSeparador(string texto) {
    for (char& c : texto) {
        if (c == '|') c = '/';
        if (c == '\r' || c == '\n') c = ' ';
    }
    return texto;
}

vector<string> separar(const string& linha, char delimitador) {
    vector<string> partes;
    string atual;
    for (char c : linha) {
        if (c == delimitador) {
            partes.push_back(atual);
            atual.clear();
        } else {
            atual += c;
        }
    }
    partes.push_back(atual);
    return partes;
}

bool inteiroValido(const string& texto, int& valor) {
    const string t = aparar(texto);
    if (t.empty()) return false;

    try {
        size_t pos = 0;
        const int numero = stoi(t, &pos);
        if (pos != t.size()) return false;
        valor = numero;
        return true;
    } catch (...) {
        return false;
    }
}

string dataHoje() {
    const time_t agora = time(nullptr);
    const tm* local = localtime(&agora);
    char buffer[16]{};
    strftime(buffer, sizeof(buffer), "%d/%m/%Y", local);
    return buffer;
}

bool dataValida(const string& data) {
    if (data.size() != 10 || data[2] != '/' || data[5] != '/') return false;

    for (size_t i = 0; i < data.size(); ++i) {
        if (i == 2 || i == 5) continue;
        if (!isdigit(static_cast<unsigned char>(data[i]))) return false;
    }

    const int dia = stoi(data.substr(0, 2));
    const int mes = stoi(data.substr(3, 2));
    const int ano = stoi(data.substr(6, 4));

    if (ano < config::ANO_MINIMO || ano > config::ANO_MAXIMO) return false;
    if (mes < 1 || mes > 12) return false;

    int diasMes = 31;
    if (mes == 4 || mes == 6 || mes == 9 || mes == 11) diasMes = 30;
    if (mes == 2) {
        const bool bissexto = (ano % 400 == 0) || (ano % 4 == 0 && ano % 100 != 0);
        diasMes = bissexto ? 29 : 28;
    }

    return dia >= 1 && dia <= diasMes;
}

template <typename T>
int proximoId(const vector<T>& itens) {
    int maior = 0;
    for (const auto& item : itens) maior = max(maior, item.id);
    return maior + 1;
}

// ============================================================================
// 2. REGRAS DE NEGÓCIO — ESTOQUE
// ============================================================================
struct VisaoEstoque {
    Peca peca;
    int quantidade = 0;
    SituacaoEstoque situacao = SituacaoEstoque::OK;
    int comprar = 0;
};

class ControleEstoque {
public:
    explicit ControleEstoque(BancoDados& banco) : dados(banco) {}

    static SituacaoEstoque situacao(int quantidade) {
        if (quantidade <= 0) return SituacaoEstoque::SEM_ESTOQUE;
        if (quantidade < config::ESTOQUE_MINIMO) return SituacaoEstoque::CRITICO;
        return SituacaoEstoque::OK;
    }

    static int quantidadeParaComprar(int quantidade) {
        return max(0, config::ESTOQUE_MINIMO - quantidade);
    }

    const Peca* peca(int id) const {
        for (const auto& p : dados.pecas)
            if (p.id == id) return &p;
        return nullptr;
    }

    Peca* pecaMutavel(int id) {
        for (auto& p : dados.pecas)
            if (p.id == id) return &p;
        return nullptr;
    }

    const ItemEstoque* item(int idPeca) const {
        for (const auto& i : dados.estoque)
            if (i.idPeca == idPeca) return &i;
        return nullptr;
    }

    ItemEstoque* itemMutavel(int idPeca) {
        for (auto& i : dados.estoque)
            if (i.idPeca == idPeca) return &i;
        return nullptr;
    }

    Resultado cadastrarPeca(const string& nome, int quantidade, int* idCriado = nullptr) {
        const string nomeLimpo = aparar(nome);
        if (nomeLimpo.empty()) return Resultado::Falha("Informe o nome da peça.");
        if (quantidade < 0) return Resultado::Falha("A quantidade não pode ser negativa.");
        if (quantidade > config::LIMITE_QUANTIDADE)
            return Resultado::Falha("Quantidade acima do limite permitido.");

        for (const auto& p : dados.pecas) {
            if (mesmoTexto(p.nome, nomeLimpo))
                return Resultado::Falha("Já existe uma peça com este nome.");
        }

        Peca p;
        p.id = proximoId(dados.pecas);
        p.nome = nomeLimpo;

        dados.pecas.push_back(p);
        dados.estoque.push_back({p.id, quantidade});
        dados.marcarAlteracao();

        if (idCriado) *idCriado = p.id;

        string mensagem = "Peça \"" + p.nome + "\" cadastrada.";
        const string alerta = alertaEstoque(p.id);
        if (!alerta.empty()) mensagem += "\n\n" + alerta;
        return Resultado::Sucesso(mensagem);
    }

    Resultado editarQuantidade(int idPeca, int quantidade) {
        ItemEstoque* itemEstoque = itemMutavel(idPeca);
        const Peca* p = peca(idPeca);
        if (!itemEstoque || !p)
            return Resultado::Falha("Peça não encontrada no estoque.");
        if (quantidade < 0)
            return Resultado::Falha("A quantidade não pode ser negativa.");
        if (quantidade > config::LIMITE_QUANTIDADE)
            return Resultado::Falha("Quantidade acima do limite permitido.");

        itemEstoque->quantidade = quantidade;
        dados.marcarAlteracao();
        return Resultado::Sucesso(
            "Estoque da peça \"" + p->nome + "\" atualizado para " +
            to_string(quantidade) + " unidade(s).");
    }

    Resultado registrarEntrada(int idPeca, int quantidade) {
        ItemEstoque* itemEstoque = itemMutavel(idPeca);
        const Peca* p = peca(idPeca);

        if (!itemEstoque || !p) return Resultado::Falha("Peça não encontrada no estoque.");
        if (quantidade <= 0) return Resultado::Falha("A quantidade de entrada deve ser maior que zero.");
        if (quantidade > config::LIMITE_QUANTIDADE - itemEstoque->quantidade)
            return Resultado::Falha("Quantidade total excede o limite permitido.");

        itemEstoque->quantidade += quantidade;
        dados.marcarAlteracao();
        return Resultado::Sucesso(
            "Entrada registrada para \"" + p->nome + "\". Estoque atual: " +
            to_string(itemEstoque->quantidade) + ".");
    }

    // Única operação autorizada a retirar peças do estoque.
    Resultado retirar(int idPeca, int quantidade) {
        const Resultado verificacao = verificarSaida(idPeca, quantidade);
        if (!verificacao.ok) return verificacao;

        itemMutavel(idPeca)->quantidade -= quantidade;
        dados.marcarAlteracao();
        return Resultado::Sucesso();
    }

    Resultado verificarSaida(int idPeca, int quantidade) const {
        const ItemEstoque* itemEstoque = item(idPeca);
        const Peca* p = peca(idPeca);

        if (!itemEstoque || !p)
            return Resultado::Falha("Peça não encontrada no estoque.");
        if (quantidade <= 0)
            return Resultado::Falha("A quantidade deve ser maior que zero.");
        if (quantidade > itemEstoque->quantidade) {
            return Resultado::Falha(
                "Operação não realizada.\n\nPeça: " + p->nome +
                "\nEstoque disponível: " + to_string(itemEstoque->quantidade) +
                "\nQuantidade solicitada: " + to_string(quantidade) +
                "\n\nO estoque não pode ficar negativo.");
        }

        return Resultado::Sucesso();
    }

    vector<VisaoEstoque> listar() const {
        vector<VisaoEstoque> resultado;

        for (const auto& p : dados.pecas) {
            const ItemEstoque* itemEstoque = item(p.id);
            if (!itemEstoque) continue;

            VisaoEstoque v;
            v.peca = p;
            v.quantidade = itemEstoque->quantidade;
            v.situacao = situacao(v.quantidade);
            v.comprar = quantidadeParaComprar(v.quantidade);
            resultado.push_back(v);
        }

        sort(resultado.begin(), resultado.end(), [](const VisaoEstoque& a, const VisaoEstoque& b) {
            return minusculas(a.peca.nome) < minusculas(b.peca.nome);
        });

        return resultado;
    }

    vector<VisaoEstoque> criticos() const {
        vector<VisaoEstoque> resultado;
        for (const auto& item : listar()) {
            if (item.situacao != SituacaoEstoque::OK)
                resultado.push_back(item);
        }
        return resultado;
    }

    long long totalUnidades() const {
        long long total = 0;
        for (const auto& itemEstoque : dados.estoque) total += itemEstoque.quantidade;
        return total;
    }

    int totalPecas() const { return static_cast<int>(dados.pecas.size()); }

    int totalCriticos() const { return static_cast<int>(criticos().size()); }

    string alertaEstoque(int idPeca) const {
        const ItemEstoque* itemEstoque = item(idPeca);
        const Peca* p = peca(idPeca);
        if (!itemEstoque || !p) return "";

        const int comprar = quantidadeParaComprar(itemEstoque->quantidade);
        if (comprar <= 0) return "";

        return "ATENÇÃO: \"" + p->nome + "\" está " +
               nomeSituacao(situacao(itemEstoque->quantidade)) +
               ". É necessário comprar " + to_string(comprar) + " unidade(s).";
    }

private:
    BancoDados& dados;
};

// ============================================================================
// 2. REGRAS DE NEGÓCIO — MÁQUINAS E RELACIONAMENTOS
// ============================================================================
struct DadosMaquina {
    string nome;
};

class ControleMaquinas {
public:
    explicit ControleMaquinas(BancoDados& banco) : dados(banco) {}

    const vector<Maquina>& lista() const { return dados.maquinas; }

    const Maquina* maquina(int id) const {
        for (const auto& m : dados.maquinas)
            if (m.id == id) return &m;
        return nullptr;
    }

    Maquina* maquinaMutavel(int id) {
        for (auto& m : dados.maquinas)
            if (m.id == id) return &m;
        return nullptr;
    }

    Resultado cadastrar(const DadosMaquina& entrada, const vector<MaquinaPeca>& pecasNecessarias = {}) {
        if (aparar(entrada.nome).empty())
            return Resultado::Falha("Informe o nome da máquina.");

        // O ID é interno e automático. Não é solicitado nem exibido ao usuário.
        const int idInterno = proximoId(dados.maquinas);

        // As peças relacionadas devem existir previamente no cadastro global.
        // O relacionamento representa apenas a quantidade necessária para
        // manutenção/conserto da máquina, nunca estoque da máquina.
        for (const auto& rel : pecasNecessarias) {
            if (rel.idMaquina != idInterno)
                return Resultado::Falha("Relacionamento de peça inválido para esta máquina.");
            if (!pecaExiste(rel.idPeca))
                return Resultado::Falha("Uma das peças selecionadas não está cadastrada no sistema.");
            if (rel.quantidadeNecessaria <= 0 || rel.quantidadeNecessaria > config::LIMITE_QUANTIDADE)
                return Resultado::Falha("A quantidade necessária de cada peça deve ser maior que zero.");
        }

        Maquina m;
        m.id = idInterno;
        m.nome = aparar(entrada.nome);
        dados.maquinas.push_back(m);

        for (const auto& rel : pecasNecessarias)
            dados.relacoes.push_back(rel);

        dados.marcarAlteracao();

        return Resultado::Sucesso(
            "Máquina \"" + m.nome + "\" cadastrada com " +
            to_string(pecasNecessarias.size()) + " peça(s) necessária(s).");
    }

    Resultado editar(int id, const DadosMaquina& entrada) {
        Maquina* m = maquinaMutavel(id);
        if (!m) return Resultado::Falha("Máquina não encontrada.");
        if (aparar(entrada.nome).empty())
            return Resultado::Falha("Informe o nome da máquina.");

        m->nome = aparar(entrada.nome);
        for (auto& v : dados.vendas) {
            if (v.idMaquina == id) v.nomeMaquina = m->nome;
        }

        dados.marcarAlteracao();
        return Resultado::Sucesso("Máquina atualizada.");
    }

    Resultado remover(int id) {
        const Maquina* m = maquina(id);
        if (!m) return Resultado::Falha("Máquina não encontrada.");

        const string nome = m->nome;

        dados.relacoes.erase(
            remove_if(dados.relacoes.begin(), dados.relacoes.end(),
                      [&](const MaquinaPeca& rel) { return rel.idMaquina == id; }),
            dados.relacoes.end());

        dados.maquinas.erase(
            remove_if(dados.maquinas.begin(), dados.maquinas.end(),
                      [&](const Maquina& maquinaItem) { return maquinaItem.id == id; }),
            dados.maquinas.end());

        dados.marcarAlteracao();
        return Resultado::Sucesso("Máquina \"" + nome + "\" removida.");
    }

    vector<MaquinaPeca> pecasDaMaquina(int idMaquina) const {
        vector<MaquinaPeca> resultado;
        for (const auto& rel : dados.relacoes)
            if (rel.idMaquina == idMaquina) resultado.push_back(rel);
        return resultado;
    }

    bool possuiRelacao(int idMaquina, int idPeca) const {
        for (const auto& rel : dados.relacoes) {
            if (rel.idMaquina == idMaquina && rel.idPeca == idPeca) return true;
        }
        return false;
    }

    Resultado relacionarPeca(int idMaquina, int idPeca, int quantidade) {
        if (!maquina(idMaquina)) return Resultado::Falha("Máquina não encontrada.");
        if (quantidade <= 0) return Resultado::Falha("A quantidade necessária deve ser maior que zero.");
        if (quantidade > config::LIMITE_QUANTIDADE)
            return Resultado::Falha("Quantidade acima do limite permitido.");
        if (!pecaExiste(idPeca)) return Resultado::Falha("Peça não encontrada.");
        if (possuiRelacao(idMaquina, idPeca))
            return Resultado::Falha("Esta peça já está relacionada à máquina.");

        dados.relacoes.push_back({idMaquina, idPeca, quantidade});
        dados.marcarAlteracao();
        return Resultado::Sucesso("Peça relacionada à máquina.");
    }

    Resultado alterarQuantidade(int idMaquina, int idPeca, int quantidade) {
        for (auto& rel : dados.relacoes) {
            if (rel.idMaquina == idMaquina && rel.idPeca == idPeca) {
                if (quantidade <= 0) return Resultado::Falha("A quantidade deve ser maior que zero.");
                if (quantidade > config::LIMITE_QUANTIDADE)
                    return Resultado::Falha("Quantidade acima do limite permitido.");
                rel.quantidadeNecessaria = quantidade;
                dados.marcarAlteracao();
                return Resultado::Sucesso("Quantidade necessária atualizada.");
            }
        }
        return Resultado::Falha("Relacionamento não encontrado.");
    }

    Resultado removerRelacao(int idMaquina, int idPeca) {
        const auto antes = dados.relacoes.size();
        dados.relacoes.erase(
            remove_if(dados.relacoes.begin(), dados.relacoes.end(),
                      [&](const MaquinaPeca& rel) {
                          return rel.idMaquina == idMaquina && rel.idPeca == idPeca;
                      }),
            dados.relacoes.end());

        if (dados.relacoes.size() == antes)
            return Resultado::Falha("Relacionamento não encontrado.");

        dados.marcarAlteracao();
        return Resultado::Sucesso("Peça removida da máquina.");
    }

private:
    BancoDados& dados;

    bool pecaExiste(int idPeca) const {
        for (const auto& p : dados.pecas)
            if (p.id == idPeca) return true;
        return false;
    }
};

// ============================================================================
// 2. REGRAS DE NEGÓCIO — MANUTENÇÃO
// ============================================================================
struct DiagnosticoPeca {
    int idPeca = 0;
    string nome;
    int quantidadeNecessaria = 0;
    int estoque = 0;
    bool suficiente = false;
};

class ControleManutencao {
public:
    ControleManutencao(BancoDados& banco, ControleEstoque& estoqueControle)
        : dados(banco), estoque(estoqueControle) {}

    const vector<Manutencao>& historico() const { return dados.manutencoes; }

    vector<DiagnosticoPeca> diagnosticar(int idMaquina) const {
        vector<DiagnosticoPeca> resultado;

        for (const auto& rel : dados.relacoes) {
            if (rel.idMaquina != idMaquina) continue;

            const Peca* p = estoque.peca(rel.idPeca);
            const ItemEstoque* item = estoque.item(rel.idPeca);
            if (!p || !item) continue;

            resultado.push_back({
                p->id,
                p->nome,
                rel.quantidadeNecessaria,
                item->quantidade,
                item->quantidade >= rel.quantidadeNecessaria
            });
        }

        return resultado;
    }

    Resultado registrar(const Manutencao& entrada) {
        if (entrada.idMaquina <= 0)
            return Resultado::Falha("Máquina inválida.");
        if (!maquinaExiste(entrada.idMaquina))
            return Resultado::Falha("A máquina selecionada não está cadastrada.");
        if (!dataValida(aparar(entrada.data)))
            return Resultado::Falha("Data inválida. Use o formato DD/MM/AAAA.");
        map<int, int> totais;
        for (const auto& usada : entrada.pecas) {
            if (usada.quantidade <= 0)
                return Resultado::Falha("A quantidade utilizada de cada peça deve ser maior que zero.");
            if (usada.quantidade > config::LIMITE_QUANTIDADE)
                return Resultado::Falha("Quantidade utilizada acima do limite permitido.");
            totais[usada.idPeca] += usada.quantidade;
        }

        // Primeiro valida TODAS as saídas.
        for (const auto& [idPeca, quantidade] : totais) {
            const Resultado validacao = estoque.verificarSaida(idPeca, quantidade);
            if (!validacao.ok) return validacao;
        }

        // Só depois das validações faz as baixas.
        Manutencao m = entrada;
        m.id = proximoId(dados.manutencoes);
        m.data = aparar(m.data);
        m.observacao = aparar(m.observacao);
        m.pecas.clear();

        for (const auto& [idPeca, quantidade] : totais) {
            const Resultado baixa = estoque.retirar(idPeca, quantidade);
            if (!baixa.ok)
                return Resultado::Falha("Não foi possível concluir a baixa das peças.");
            m.pecas.push_back({idPeca, quantidade});
        }

        dados.manutencoes.push_back(m);
        dados.marcarAlteracao();

        string mensagem = "Manutenção registrada com sucesso.";
        if (!m.pecas.empty()) {
            mensagem += "\nPeças retiradas do estoque: " + to_string(m.pecas.size()) + " tipo(s).";
        }
        return Resultado::Sucesso(mensagem);
    }

private:
    BancoDados& dados;
    ControleEstoque& estoque;

    bool maquinaExiste(int id) const {
        for (const auto& m : dados.maquinas)
            if (m.id == id) return true;
        return false;
    }
};

// ============================================================================
// 2. REGRAS DE NEGÓCIO — VENDAS E RANKINGS
// ============================================================================
struct RankingCliente {
    string nome;
    int total = 0;
};

struct RankingMaquina {
    int idMaquina = 0;
    string nome;
    int total = 0;
};

class ControleVendas {
public:
    ControleVendas(BancoDados& banco, ControleMaquinas& maquinasControle)
        : dados(banco), maquinas(maquinasControle) {}

    const vector<Venda>& lista() const { return dados.vendas; }

    int totalMaquinasVendidas() const {
        int total = 0;
        for (const auto& venda : dados.vendas) {
            if (!venda.registroAntigoDePecas) total += venda.quantidade;
        }
        return total;
    }

    Resultado registrar(int idMaquina, int quantidade, const string& cliente) {
        const Maquina* maquina = maquinas.maquina(idMaquina);
        if (!maquina) return Resultado::Falha("Selecione uma máquina cadastrada.");
        if (aparar(cliente).empty()) return Resultado::Falha("Informe o nome do cliente.");
        if (quantidade <= 0) return Resultado::Falha("A quantidade de máquinas vendidas deve ser maior que zero.");
        if (quantidade > config::LIMITE_QUANTIDADE)
            return Resultado::Falha("Quantidade acima do limite permitido.");

        Venda venda;
        venda.idMaquina = idMaquina;
        venda.quantidade = quantidade;
        venda.cliente = aparar(cliente);
        venda.nomeMaquina = maquina->nome;

        dados.vendas.push_back(venda);
        dados.marcarAlteracao();

        string mensagem = "Venda registrada com sucesso.\n\n";
        mensagem += "Cliente: " + venda.cliente + "\n";
        mensagem += "Máquina: " + venda.nomeMaquina + "\n";
        mensagem += "Quantidade de máquinas vendidas: " + to_string(venda.quantidade);
        return Resultado::Sucesso(mensagem);
    }

    vector<Venda> consultar(const string& cliente = "", int idMaquina = 0) const {
        vector<Venda> resultado;
        for (const auto& venda : dados.vendas) {
            if (!cliente.empty() && !mesmoTexto(venda.cliente, cliente)) continue;
            if (idMaquina != 0 && venda.idMaquina != idMaquina) continue;
            resultado.push_back(venda);
        }
        return resultado;
    }

    vector<RankingCliente> rankingClientes() const {
        unordered_map<string, int> totais;
        for (const auto& venda : dados.vendas) {
            if (!venda.registroAntigoDePecas)
                totais[venda.cliente] += venda.quantidade;
        }

        vector<RankingCliente> resultado;
        for (const auto& [cliente, total] : totais)
            resultado.push_back({cliente, total});

        sort(resultado.begin(), resultado.end(), [](const RankingCliente& a, const RankingCliente& b) {
            if (a.total != b.total) return a.total > b.total;
            return minusculas(a.nome) < minusculas(b.nome);
        });
        return resultado;
    }

    // Ranking por quantidade de máquinas vendidas de cada modelo cadastrado.
    vector<RankingMaquina> rankingMaquinas() const {
        unordered_map<int, RankingMaquina> acumulado;

        for (const auto& venda : dados.vendas) {
            if (venda.registroAntigoDePecas || venda.idMaquina <= 0) continue;

            auto it = acumulado.find(venda.idMaquina);
            if (it == acumulado.end()) {
                acumulado[venda.idMaquina] = {
                    venda.idMaquina,
                    venda.nomeMaquina,
                    venda.quantidade
                };
            } else {
                it->second.total += venda.quantidade;
            }
        }

        vector<RankingMaquina> resultado;
        resultado.reserve(acumulado.size());
        for (const auto& [id, ranking] : acumulado)
            resultado.push_back(ranking);

        sort(resultado.begin(), resultado.end(), [](const RankingMaquina& a, const RankingMaquina& b) {
            if (a.total != b.total) return a.total > b.total;
            return a.idMaquina < b.idMaquina;
        });

        return resultado;
    }

private:
    BancoDados& dados;
    ControleMaquinas& maquinas;
};

// ============================================================================
// 3. PERSISTÊNCIA
// ============================================================================
class Repositorio {
public:
    bool primeiraExecucao() const {
        return !ifstream(config::ARQ_MAQUINAS).is_open() &&
               !ifstream(config::ARQ_PECAS).is_open();
    }

    bool carregarTudo(BancoDados& db) const {
        db = BancoDados{};
        bool ok = true;
        ok = carregarMaquinas(db) && ok;
        ok = carregarPecas(db) && ok;
        ok = carregarEstoque(db) && ok;
        ok = carregarRelacoes(db) && ok;
        ok = carregarVendas(db) && ok;
        ok = carregarManutencoes(db) && ok;
        sanear(db);
        db.alterado = false;
        return ok;
    }

    bool salvarTudo(const BancoDados& db) const {
        bool ok = true;
        ok = salvarMaquinas(db) && ok;
        ok = salvarPecas(db) && ok;
        ok = salvarEstoque(db) && ok;
        ok = salvarRelacoes(db) && ok;
        ok = salvarVendas(db) && ok;
        ok = salvarManutencoes(db) && ok;
        return ok;
    }

private:
    template <typename Funcao>
    static bool lerLinhas(const string& caminho, Funcao processar) {
        ifstream arquivo(caminho);
        if (!arquivo.is_open()) return true;

        string linha;
        while (getline(arquivo, linha)) {
            if (aparar(linha).empty()) continue;
            processar(separar(linha, '|'));
        }
        return !arquivo.bad();
    }

    template <typename Funcao>
    static bool gravar(const string& caminho, Funcao escrever) {
        ofstream arquivo(caminho, ios::out | ios::trunc);
        if (!arquivo.is_open()) return false;
        escrever(arquivo);
        return arquivo.good();
    }

    static bool carregarMaquinas(BancoDados& db) {
        return lerLinhas(config::ARQ_MAQUINAS, [&](const vector<string>& c) {
            if (c.size() < 2) return;
            int id = 0;
            if (!inteiroValido(c[0], id) || id <= 0 || aparar(c[1]).empty()) return;
            if (any_of(db.maquinas.begin(), db.maquinas.end(), [&](const Maquina& m) { return m.id == id; })) return;
            db.maquinas.push_back({id, aparar(c[1])});
        });
    }

    static bool salvarMaquinas(const BancoDados& db) {
        return gravar(config::ARQ_MAQUINAS, [&](ofstream& arquivo) {
            for (const auto& m : db.maquinas)
                arquivo << m.id << '|' << limparSeparador(m.nome) << '\n';
        });
    }

    static bool carregarPecas(BancoDados& db) {
        return lerLinhas(config::ARQ_PECAS, [&](const vector<string>& c) {
            if (c.size() < 2) return;
            int id = 0;
            if (!inteiroValido(c[0], id) || id <= 0 || aparar(c[1]).empty()) return;
            if (any_of(db.pecas.begin(), db.pecas.end(), [&](const Peca& p) { return p.id == id; })) return;
            db.pecas.push_back({id, aparar(c[1])});
        });
    }

    static bool salvarPecas(const BancoDados& db) {
        return gravar(config::ARQ_PECAS, [&](ofstream& arquivo) {
            for (const auto& p : db.pecas)
                arquivo << p.id << '|' << limparSeparador(p.nome) << '\n';
        });
    }

    static bool carregarEstoque(BancoDados& db) {
        ifstream arquivo(config::ARQ_ESTOQUE);
        if (!arquivo.is_open()) return true;

        string linha;
        bool primeira = true;
        bool formatoNovo = false;

        while (getline(arquivo, linha)) {
            if (aparar(linha).empty()) continue;

            if (primeira) {
                primeira = false;
                if (aparar(linha) == "#v3") {
                    formatoNovo = true;
                    continue;
                }
            }

            const vector<string> c = separar(linha, '|');
            if (formatoNovo) {
                ItemEstoque item;
                if (c.size() < 2 ||
                    !inteiroValido(c[0], item.idPeca) ||
                    !inteiroValido(c[1], item.quantidade) ||
                    item.idPeca <= 0 || item.quantidade < 0)
                    continue;
                db.estoque.push_back(item);
            } else {
                // Compatibilidade simples com o formato antigo nome|quantidade|observacao.
                int quantidade = 0;
                if (c.size() < 2 || aparar(c[0]).empty() || !inteiroValido(c[1], quantidade) || quantidade < 0)
                    continue;

                Peca p;
                p.id = proximoId(db.pecas);
                p.nome = aparar(c[0]);
                db.pecas.push_back(p);
                db.estoque.push_back({p.id, quantidade});
            }
        }

        return !arquivo.bad();
    }

    static bool salvarEstoque(const BancoDados& db) {
        return gravar(config::ARQ_ESTOQUE, [&](ofstream& arquivo) {
            arquivo << "#v3\n";
            for (const auto& item : db.estoque)
                arquivo << item.idPeca << '|' << item.quantidade << '\n';
        });
    }

    static bool carregarRelacoes(BancoDados& db) {
        return lerLinhas(config::ARQ_RELACOES, [&](const vector<string>& c) {
            if (c.size() < 3) return;
            MaquinaPeca rel;
            if (!inteiroValido(c[0], rel.idMaquina) ||
                !inteiroValido(c[1], rel.idPeca) ||
                !inteiroValido(c[2], rel.quantidadeNecessaria)) return;
            if (rel.idMaquina <= 0 || rel.idPeca <= 0 || rel.quantidadeNecessaria <= 0) return;
            db.relacoes.push_back(rel);
        });
    }

    static bool salvarRelacoes(const BancoDados& db) {
        return gravar(config::ARQ_RELACOES, [&](ofstream& arquivo) {
            for (const auto& rel : db.relacoes)
                arquivo << rel.idMaquina << '|' << rel.idPeca << '|' << rel.quantidadeNecessaria << '\n';
        });
    }

    static bool carregarVendas(BancoDados& db) {
        return lerLinhas(config::ARQ_VENDAS, [&](const vector<string>& c) {
            if (c.empty() || (!c[0].empty() && c[0][0] == '#')) return;

            // Compatibilidade com o formato antigo com peças: preserva o histórico,
            // sem mostrar nem movimentar peças na tela de vendas.
            if (c.size() >= 6) {
                Venda v;
                if (!inteiroValido(c[0], v.idMaquina) ||
                    !inteiroValido(c[2], v.quantidade) ||
                    v.idMaquina <= 0 || v.quantidade <= 0 || aparar(c[3]).empty()) return;
                v.cliente = aparar(c[3]);
                v.nomeMaquina = aparar(c[4]);
                v.registroAntigoDePecas = true;
                db.vendas.push_back(v);
                return;
            }

            // Formato atual: idMaquina|quantidade|cliente|nomeMaquina|registroAntigo
            if (c.size() >= 4) {
                Venda v;
                if (!inteiroValido(c[0], v.idMaquina) ||
                    !inteiroValido(c[1], v.quantidade) ||
                    v.idMaquina <= 0 || v.quantidade <= 0 || aparar(c[2]).empty()) return;
                v.cliente = aparar(c[2]);
                v.nomeMaquina = aparar(c[3]);
                if (c.size() >= 5) v.registroAntigoDePecas = (c[4] == "1");
                db.vendas.push_back(v);
                return;
            }

            // Compatibilidade com o formato antigo item|quantidade|cliente.
            if (c.size() >= 3) {
                int quantidade = 0;
                if (!inteiroValido(c[1], quantidade) || quantidade <= 0 || aparar(c[2]).empty()) return;
                Venda v;
                v.idMaquina = 0;
                v.quantidade = quantidade;
                v.cliente = aparar(c[2]);
                v.nomeMaquina = "Máquina não informada";
                v.registroAntigoDePecas = true;
                db.vendas.push_back(v);
            }
        });
    }

    static bool salvarVendas(const BancoDados& db) {
        return gravar(config::ARQ_VENDAS, [&](ofstream& arquivo) {
            arquivo << "#v3\n";
            for (const auto& v : db.vendas) {
                arquivo << v.idMaquina << '|'
                        << v.quantidade << '|'
                        << limparSeparador(v.cliente) << '|'
                        << limparSeparador(v.nomeMaquina) << '|'
                        << (v.registroAntigoDePecas ? 1 : 0) << '\n';
            }
        });
    }

    static bool carregarManutencoes(BancoDados& db) {
        bool formatoNovo = false;
        return lerLinhas(config::ARQ_MANUTENCOES, [&](const vector<string>& c) {
            if (c.empty()) return;
            if (c[0] == "#v2") {
                formatoNovo = true;
                return;
            }

            Manutencao m;
            if (!inteiroValido(c[0], m.id) ||
                c.size() < 4 || !inteiroValido(c[1], m.idMaquina) ||
                m.id <= 0 || m.idMaquina <= 0) return;

            m.data = aparar(c[2]);
            size_t indicePecas = 0;
            if (formatoNovo) {
                m.observacao = aparar(c[3]);
                if (c.size() < 5) return;
                indicePecas = 4;
            } else {
                // Lê o formato antigo id|máquina|data|problema|serviço|observação|peças.
                if (c.size() < 7) return;
                m.observacao = aparar(c[5]);
                indicePecas = 6;
            }

            for (const string& par : separar(c[indicePecas], ';')) {
                const vector<string> kv = separar(par, ':');
                if (kv.size() != 2) continue;

                PecaUsada usada;
                if (inteiroValido(kv[0], usada.idPeca) &&
                    inteiroValido(kv[1], usada.quantidade) &&
                    usada.idPeca > 0 && usada.quantidade > 0) {
                    m.pecas.push_back(usada);
                }
            }
            db.manutencoes.push_back(m);
        });
    }

    static bool salvarManutencoes(const BancoDados& db) {
        return gravar(config::ARQ_MANUTENCOES, [&](ofstream& arquivo) {
            arquivo << "#v2\n";
            for (const auto& m : db.manutencoes) {
                arquivo << m.id << '|'
                        << m.idMaquina << '|'
                        << limparSeparador(m.data) << '|'
                        << limparSeparador(m.observacao) << '|';

                for (size_t i = 0; i < m.pecas.size(); ++i) {
                    if (i > 0) arquivo << ';';
                    arquivo << m.pecas[i].idPeca << ':' << m.pecas[i].quantidade;
                }
                arquivo << '\n';
            }
        });
    }

    static void sanear(BancoDados& db) {
        auto pecaExiste = [&](int id) {
            return any_of(db.pecas.begin(), db.pecas.end(),
                          [&](const Peca& p) { return p.id == id; });
        };
        auto maquinaExiste = [&](int id) {
            return any_of(db.maquinas.begin(), db.maquinas.end(),
                          [&](const Maquina& m) { return m.id == id; });
        };

        // Cada peça precisa possuir uma posição global de estoque.
        db.estoque.erase(
            remove_if(db.estoque.begin(), db.estoque.end(),
                      [&](const ItemEstoque& item) {
                          return !pecaExiste(item.idPeca) || item.quantidade < 0;
                      }),
            db.estoque.end());

        for (const auto& p : db.pecas) {
            const bool possui = any_of(db.estoque.begin(), db.estoque.end(),
                                       [&](const ItemEstoque& item) { return item.idPeca == p.id; });
            if (!possui) db.estoque.push_back({p.id, 0});
        }

        vector<MaquinaPeca> relacoesValidas;
        for (const auto& rel : db.relacoes) {
            if (!maquinaExiste(rel.idMaquina) || !pecaExiste(rel.idPeca) || rel.quantidadeNecessaria <= 0)
                continue;

            const bool duplicada = any_of(relacoesValidas.begin(), relacoesValidas.end(),
                                          [&](const MaquinaPeca& outra) {
                                              return outra.idMaquina == rel.idMaquina &&
                                                     outra.idPeca == rel.idPeca;
                                          });
            if (!duplicada) relacoesValidas.push_back(rel);
        }
        db.relacoes = relacoesValidas;

        // Mantém o nome da máquina atualizado quando ela ainda está cadastrada.
        for (auto& venda : db.vendas) {
            if (const Maquina* m = encontrarMaquina(db, venda.idMaquina))
                venda.nomeMaquina = m->nome;
        }
    }

    static const Maquina* encontrarMaquina(const BancoDados& db, int id) {
        for (const auto& m : db.maquinas)
            if (m.id == id) return &m;
        return nullptr;
    }

    static const Peca* encontrarPeca(const BancoDados& db, int id) {
        for (const auto& p : db.pecas)
            if (p.id == id) return &p;
        return nullptr;
    }
};

// ============================================================================
// FACHADA
// ============================================================================
class Sistema {
public:
    Sistema()
        : estoque(dados),
          maquinas(dados),
          manutencao(dados, estoque),
          vendas(dados, maquinas) {}

    bool carregar() {
        return repositorio.carregarTudo(dados);
    }

    bool salvar() {
        const bool ok = repositorio.salvarTudo(dados);
        if (ok) dados.alterado = false;
        return ok;
    }

    BancoDados dados;
    ControleEstoque estoque;
    ControleMaquinas maquinas;
    ControleManutencao manutencao;
    ControleVendas vendas;

private:
    Repositorio repositorio;
};

// ============================================================================
// 4. INTERFACE FTXUI
// ============================================================================
namespace ui {

struct ResultadoFormulario {
    bool confirmado = false;
    vector<string> valores;
};

Element titulo(const string& texto) {
    return window(text("  " + texto + "  ") | bold | color(Color::White), text("")) |
           bgcolor(Color::RGB(35, 45, 65));
}

Element card(const string& nome, const string& valor, Color cor) {
    return window(text(" " + nome + " ") | bold | color(cor),
                  text("  " + valor + "  ") | bold | center) |
           size(WIDTH, GREATER_THAN, 18) | flex;
}

Color corSituacao(SituacaoEstoque situacao) {
    if (situacao == SituacaoEstoque::OK) return Color::Green;
    if (situacao == SituacaoEstoque::CRITICO) return Color::Yellow;
    return Color::Red;
}

void pausarMensagem(const string& cabecalho, const string& mensagem, bool erro = false) {
    auto fechar = Button("OK", [] {});
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(fechar, [&] {
        return window(text(" " + cabecalho + " ") | bold |
                          color(erro ? Color::Red : Color::Green),
                      vbox({paragraph(mensagem) | center,
                            separator(),
                            fechar->Render() | center})) |
               size(WIDTH, GREATER_THAN, 70) | center;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Return || event == Event::Escape) {
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

ResultadoFormulario formularioSimples(
    const string& nomeFormulario,
    const vector<pair<string, string>>& campos,
    const string& textoBotao = "Salvar") {

    ResultadoFormulario resultado;
    resultado.valores.reserve(campos.size());

    vector<Component> inputs;
    inputs.reserve(campos.size());

    for (const auto& campo : campos) {
        resultado.valores.push_back(campo.second);
        inputs.push_back(Input(&resultado.valores.back(), campo.first));
    }

    auto tela = ScreenInteractive::Fullscreen();

    auto salvar = Button(textoBotao, [&] {
        resultado.confirmado = true;
        tela.Exit();
    });

    auto cancelar = Button("Cancelar", [&] {
        resultado.confirmado = false;
        tela.Exit();
    });

    auto botoes = Container::Horizontal({salvar, cancelar});
    auto camposContainer = Container::Vertical(inputs);
    auto raiz = Container::Vertical({camposContainer, botoes});

    auto app = Renderer(raiz, [&] {
        Elements linhas;
        for (size_t i = 0; i < campos.size(); ++i) {
            linhas.push_back(
                hbox({text(campos[i].first + ": ") | size(WIDTH, EQUAL, 27),
                      inputs[i]->Render() | flex}) |
                border | size(HEIGHT, EQUAL, 3));
        }

        return window(text(" " + nomeFormulario + " ") | bold | color(Color::Cyan),
                      vbox({vbox(std::move(linhas)),
                            separator(),
                            botoes->Render() | center,
                            text("Tab = avançar | Enter = selecionar | Esc = cancelar") |
                                color(Color::GrayDark) | center})) |
               size(WIDTH, GREATER_THAN, 78) | center;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            resultado.confirmado = false;
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
    return resultado;
}

bool confirmar(const string& cabecalho, const string& mensagem) {
    vector<string> opcoes = {"SIM", "NÃO"};
    int selecionado = 1;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    bool resposta = false;

    auto app = Renderer(menu, [&] {
        return window(text(" " + cabecalho + " ") | bold | color(Color::Yellow),
                      vbox({paragraph(mensagem) | center,
                            separator(),
                            menu->Render() | center,
                            separator(),
                            text("Enter = confirmar | Esc = cancelar") |
                                color(Color::GrayDark) | center})) |
               size(WIDTH, GREATER_THAN, 70) | center;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            resposta = false;
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            resposta = selecionado == 0;
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
    return resposta;
}

int selecionar(const string& cabecalho, const vector<string>& opcoes) {
    if (opcoes.empty()) return -1;

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    int resultado = -1;

    auto app = Renderer(menu, [&] {
        return vbox({titulo(cabecalho),
                     menu->Render() | frame | vscroll_indicator | border | flex,
                     separator(),
                     text("Setas/Tab = navegar | Enter = selecionar | Esc = voltar") |
                         color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            resultado = selecionado;
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
    return resultado;
}

void executarTela(const string& nome, const Component& componente) {
    auto tela = ScreenInteractive::Fullscreen();
    auto app = Renderer(componente, [&] {
        return vbox({text("SISTEMA DE GESTÃO DE ESTOQUE E MANUTENÇÃO") |
                         bold | color(Color::Cyan) | center,
                     text(nome) | bold | color(Color::Yellow) | center,
                     separator(),
                     componente->Render() | flex,
                     separator(),
                     text("Setas/Tab = navegar | Enter = selecionar | Esc = voltar") |
                         color(Color::GrayDark) | center}) |
               border | bgcolor(Color::RGB(15, 20, 30));
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

// ============================================================================
// UI — ESTOQUE
// ============================================================================
void cadastrarPecas(Sistema& sistema) {
    while (true) {
        auto r = formularioSimples(
            "ADICIONAR PEÇA",
            {{"Nome da peça", ""},
             {"Quantidade disponível", "0"}},
            "Adicionar");

        if (!r.confirmado) return;

        int quantidade = 0;
        if (!inteiroValido(r.valores[1], quantidade) || quantidade < 0) {
            pausarMensagem("Erro", "A quantidade deve ser um número inteiro maior ou igual a zero.", true);
            continue;
        }

        const Resultado res = sistema.estoque.cadastrarPeca(r.valores[0], quantidade);
        pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
        if (!res.ok) continue;

        if (!confirmar("ADICIONAR OUTRA PEÇA", "Deseja cadastrar outra peça?")) return;
    }
}

void editarEstoque(Sistema& sistema) {
    const auto pecas = sistema.estoque.listar();
    if (pecas.empty()) {
        pausarMensagem("Estoque", "Nenhuma peça cadastrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& v : pecas)
        opcoes.push_back(v.peca.nome + " | Atual: " + to_string(v.quantidade));

    const int indice = selecionar("EDITAR ESTOQUE", opcoes);
    if (indice < 0) return;

    const auto& atual = pecas[static_cast<size_t>(indice)];
    auto r = formularioSimples(
        "EDITAR ESTOQUE",
        {{"Nova quantidade", to_string(atual.quantidade)}});
    if (!r.confirmado) return;

    int quantidade = 0;
    if (!inteiroValido(r.valores[0], quantidade) || quantidade < 0) {
        pausarMensagem("Erro", "Informe uma quantidade inteira maior ou igual a zero.", true);
        return;
    }

    const Resultado res = sistema.estoque.editarQuantidade(atual.peca.id, quantidade);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void entradaEstoque(Sistema& sistema) {
    const auto pecas = sistema.estoque.listar();
    if (pecas.empty()) {
        pausarMensagem("Estoque", "Nenhuma peça cadastrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& v : pecas) {
        opcoes.push_back(v.peca.nome + " | Atual: " + to_string(v.quantidade) +
                         " | " + nomeSituacao(v.situacao));
    }

    const int indice = selecionar("ENTRADA DE ESTOQUE", opcoes);
    if (indice < 0) return;

    auto r = formularioSimples("REGISTRAR ENTRADA", {{"Quantidade de entrada", "1"}});
    if (!r.confirmado) return;

    int quantidade = 0;
    if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0) {
        pausarMensagem("Erro", "Informe uma quantidade maior que zero.", true);
        return;
    }

    const int idPeca = pecas[static_cast<size_t>(indice)].peca.id;
    const Resultado res = sistema.estoque.registrarEntrada(idPeca, quantidade);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void visualizarEstoque(Sistema& sistema) {
    const auto pecas = sistema.estoque.listar();
    if (pecas.empty()) {
        pausarMensagem("Estoque", "Nenhuma peça cadastrada.");
        return;
    }

    vector<string> linhas;
    for (const auto& v : pecas) {
        linhas.push_back(v.peca.nome +
                         " | Quantidade: " + to_string(v.quantidade) +
                         " | Mínimo: " + to_string(config::ESTOQUE_MINIMO) +
                         " | " + nomeSituacao(v.situacao));
    }

    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo("PEÇAS / ESTOQUE"),
                     text("Estoque mínimo do sistema: " + to_string(config::ESTOQUE_MINIMO)) | center,
                     separator(),
                     menu->Render() | frame | vscroll_indicator | border | flex,
                     separator(),
                     text("Enter = detalhes | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const auto& v = pecas[static_cast<size_t>(selecionado)];
            string mensagem =
                "Peça: " + v.peca.nome +
                "\nQuantidade: " + to_string(v.quantidade) +
                "\nEstoque mínimo: " + to_string(config::ESTOQUE_MINIMO) +
                "\nStatus: " + nomeSituacao(v.situacao) +
                "\nNecessário comprar: " + to_string(v.comprar);
            pausarMensagem("DETALHES DA PEÇA", mensagem);
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

void telaCriticos(Sistema& sistema) {
    const auto criticos = sistema.estoque.criticos();
    if (criticos.empty()) {
        pausarMensagem("ESTOQUE", "Nenhuma peça está em situação crítica.");
        return;
    }

    vector<string> opcoes;
    for (const auto& v : criticos) {
        opcoes.push_back(v.peca.nome +
                         " | Estoque: " + to_string(v.quantidade) +
                         " | Comprar: " + to_string(v.comprar) +
                         " | " + nomeSituacao(v.situacao));
    }

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    executarTela("O QUE PRECISA COMPRAR", menu);
}

void menuEstoque(Sistema& sistema) {
    const vector<string> opcoes = {
        "Adicionar Peça",
        "Adicionar outra Peça",
        "Visualizar Peças",
        "Editar Estoque",
        "Entrada de Estoque",
        "Itens Críticos / O que Precisa Comprar",
        "Voltar"
    };

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo("PEÇAS / ESTOQUE"),
                     hbox({card("PEÇAS", to_string(sistema.estoque.totalPecas()), Color::Cyan),
                           card("CRÍTICOS", to_string(sistema.estoque.totalCriticos()), Color::Red),
                           card("UNIDADES", to_string(sistema.estoque.totalUnidades()), Color::Green)}),
                     separator(),
                     menu->Render() | border | flex,
                     separator(),
                     text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: cadastrarPecas(sistema); break;
                case 1: cadastrarPecas(sistema); break;
                case 2: visualizarEstoque(sistema); break;
                case 3: editarEstoque(sistema); break;
                case 4: entradaEstoque(sistema); break;
                case 5: telaCriticos(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

// ============================================================================
// UI — MÁQUINAS
// ============================================================================
void cadastrarMaquina(Sistema& sistema) {
    auto r = formularioSimples(
        "CADASTRAR MÁQUINA",
        {{"Nome / Identificação da máquina", ""}});
    if (!r.confirmado) return;

    const int id = proximoId(sistema.maquinas.lista());

    // O requisito determina que as peças da máquina sejam peças
    // previamente cadastradas no estoque global. Nada é gravado ainda:
    // só finalizamos o cadastro quando o usuário concluir essa etapa.
    const auto pecasCadastradas = sistema.estoque.listar();
    if (pecasCadastradas.empty()) {
        pausarMensagem(
            "Cadastro de máquina",
            "Cadastre pelo menos uma peça no sistema antes de cadastrar a máquina,\n            pois as peças necessárias devem ser peças já cadastradas.",
            true);
        return;
    }

    vector<MaquinaPeca> pecasNecessarias;
    vector<int> idsJaSelecionados;

    while (true) {
        vector<string> opcoes;
        for (const auto& p : pecasCadastradas) {
            if (find(idsJaSelecionados.begin(), idsJaSelecionados.end(), p.peca.id) != idsJaSelecionados.end())
                continue;
            opcoes.push_back(p.peca.nome + " | Estoque: " + to_string(p.quantidade));
        }

        opcoes.push_back("FINALIZAR CADASTRO");

        const int escolha = selecionar(
            "PEÇAS NECESSÁRIAS PARA CONSERTO — " + r.valores[0], opcoes);
        if (escolha < 0) return;

        if (escolha == static_cast<int>(opcoes.size()) - 1) {
            if (pecasNecessarias.empty()) {
                pausarMensagem(
                    "Cadastro incompleto",
                    "A máquina deve possuir pelo menos uma peça necessária para conserto/manutenção.",
                    true);
                continue;
            }
            break;
        }

        // Como a lista pode conter itens já selecionados, recuperamos o ID
        // correspondente pelo índice da lista filtrada.
        int contador = 0;
        int idPeca = 0;
        string nomePeca;
        for (const auto& p : pecasCadastradas) {
            if (find(idsJaSelecionados.begin(), idsJaSelecionados.end(), p.peca.id) != idsJaSelecionados.end())
                continue;
            if (contador == escolha) {
                idPeca = p.peca.id;
                nomePeca = p.peca.nome;
                break;
            }
            ++contador;
        }

        if (idPeca == 0) {
            pausarMensagem("Erro", "Peça selecionada não encontrada.", true);
            continue;
        }

        auto q = formularioSimples(
            "QUANTIDADE NECESSÁRIA — " + nomePeca,
            {{"Quantidade necessária", "1"}},
            "Adicionar peça");
        if (!q.confirmado) continue;

        int quantidadeNecessaria = 0;
        if (!inteiroValido(q.valores[0], quantidadeNecessaria) || quantidadeNecessaria <= 0) {
            pausarMensagem("Erro", "A quantidade necessária deve ser um inteiro maior que zero.", true);
            continue;
        }

        pecasNecessarias.push_back({id, idPeca, quantidadeNecessaria});
        idsJaSelecionados.push_back(idPeca);

        pausarMensagem(
            "Peça adicionada",
            nomePeca + " → " + to_string(quantidadeNecessaria) + " unidade(s) necessária(s).\n\n"
            "Escolha outra peça ou finalize o cadastro.");
    }

    const Resultado res = sistema.maquinas.cadastrar({r.valores[0]}, pecasNecessarias);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void editarMaquina(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) {
        pausarMensagem("Máquinas", "Nenhuma máquina cadastrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& m : maquinas)
        opcoes.push_back(m.nome);

    const int indice = selecionar("EDITAR MÁQUINA", opcoes);
    if (indice < 0) return;

    const Maquina m = maquinas[static_cast<size_t>(indice)];
    auto r = formularioSimples(
        "EDITAR MÁQUINA",
        {{"Nome da máquina", m.nome}});
    if (!r.confirmado) return;

    const Resultado res = sistema.maquinas.editar(m.id, {r.valores[0]});
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void removerMaquina(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) {
        pausarMensagem("Máquinas", "Nenhuma máquina cadastrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& m : maquinas)
        opcoes.push_back(m.nome);

    const int indice = selecionar("REMOVER MÁQUINA", opcoes);
    if (indice < 0) return;

    const Maquina m = maquinas[static_cast<size_t>(indice)];
    if (!confirmar("CONFIRMAR REMOÇÃO",
                   "Tem certeza que deseja remover a máquina \"" + m.nome + "\"?")) return;

    const Resultado res = sistema.maquinas.remover(m.id);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void visualizarMaquina(Sistema& sistema);
void relacionarPecaMaquina(Sistema& sistema);
void editarRelacionamento(Sistema& sistema);
void removerRelacionamento(Sistema& sistema);

void visualizarMaquina(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) {
        pausarMensagem("Máquinas", "Nenhuma máquina cadastrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& m : maquinas)
        opcoes.push_back(m.nome);

    const int indice = selecionar("SELECIONE A MÁQUINA", opcoes);
    if (indice < 0) return;

    const Maquina& m = maquinas[static_cast<size_t>(indice)];
    const auto relacoes = sistema.maquinas.pecasDaMaquina(m.id);

    string mensagem = "Máquina: " + m.nome + "\n\n";
    mensagem += "PEÇAS NECESSÁRIAS PARA MANUTENÇÃO\n";
    mensagem += "----------------------------------\n";

    if (relacoes.empty()) {
        mensagem += "Nenhuma peça relacionada.\n";
    } else {
        for (const auto& rel : relacoes) {
            const Peca* p = sistema.estoque.peca(rel.idPeca);
            if (!p) continue;
            mensagem += p->nome + " -> Necessário: " +
                         to_string(rel.quantidadeNecessaria) + " unidade(s)\n";
        }
    }

    pausarMensagem("DETALHES DA MÁQUINA", mensagem);
}

void relacionarPecaMaquina(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    const auto pecas = sistema.estoque.listar();

    if (maquinas.empty() || pecas.empty()) {
        pausarMensagem("Relacionamento",
                       "Cadastre pelo menos uma máquina e uma peça antes de criar o relacionamento.", true);
        return;
    }

    vector<string> maquinasOpcoes;
    for (const auto& m : maquinas)
        maquinasOpcoes.push_back(m.nome);

    const int im = selecionar("SELECIONE A MÁQUINA", maquinasOpcoes);
    if (im < 0) return;
    const int idMaquina = maquinas[static_cast<size_t>(im)].id;

    vector<string> pecasOpcoes;
    for (const auto& p : pecas) pecasOpcoes.push_back(p.peca.nome + " | Estoque: " + to_string(p.quantidade));

    const int ip = selecionar("SELECIONE A PEÇA", pecasOpcoes);
    if (ip < 0) return;
    const int idPeca = pecas[static_cast<size_t>(ip)].peca.id;

    auto r = formularioSimples("RELACIONAR PEÇA À MÁQUINA", {{"Quantidade necessária", "1"}});
    if (!r.confirmado) return;

    int quantidade = 0;
    if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0) {
        pausarMensagem("Erro", "Quantidade inválida.", true);
        return;
    }

    const Resultado res = sistema.maquinas.relacionarPeca(idMaquina, idPeca, quantidade);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void editarRelacionamento(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) {
        pausarMensagem("Relacionamento", "Nenhuma máquina cadastrada.");
        return;
    }

    vector<string> maquinasOpcoes;
    for (const auto& m : maquinas)
        maquinasOpcoes.push_back(m.nome);

    const int im = selecionar("SELECIONE A MÁQUINA", maquinasOpcoes);
    if (im < 0) return;
    const int idMaquina = maquinas[static_cast<size_t>(im)].id;

    const auto relacoes = sistema.maquinas.pecasDaMaquina(idMaquina);
    if (relacoes.empty()) {
        pausarMensagem("Relacionamento", "Esta máquina não possui peças relacionadas.");
        return;
    }

    vector<string> relOpcoes;
    for (const auto& rel : relacoes) {
        const Peca* p = sistema.estoque.peca(rel.idPeca);
        if (p) relOpcoes.push_back(p->nome + " | Necessária: " + to_string(rel.quantidadeNecessaria));
    }

    const int ir = selecionar("EDITAR PEÇA DA MÁQUINA", relOpcoes);
    if (ir < 0) return;

    const auto& rel = relacoes[static_cast<size_t>(ir)];
    auto r = formularioSimples("ALTERAR QUANTIDADE NECESSÁRIA",
                               {{"Quantidade necessária", to_string(rel.quantidadeNecessaria)}});
    if (!r.confirmado) return;

    int quantidade = 0;
    if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0) {
        pausarMensagem("Erro", "Quantidade inválida.", true);
        return;
    }

    const Resultado res = sistema.maquinas.alterarQuantidade(idMaquina, rel.idPeca, quantidade);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void removerRelacionamento(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) return;

    vector<string> maquinasOpcoes;
    for (const auto& m : maquinas)
        maquinasOpcoes.push_back(m.nome);

    const int im = selecionar("SELECIONE A MÁQUINA", maquinasOpcoes);
    if (im < 0) return;
    const int idMaquina = maquinas[static_cast<size_t>(im)].id;

    const auto relacoes = sistema.maquinas.pecasDaMaquina(idMaquina);
    if (relacoes.empty()) {
        pausarMensagem("Relacionamento", "Esta máquina não possui peças relacionadas.");
        return;
    }

    vector<string> relOpcoes;
    for (const auto& rel : relacoes) {
        const Peca* p = sistema.estoque.peca(rel.idPeca);
        relOpcoes.push_back(p ? p->nome : "Peça ID " + to_string(rel.idPeca));
    }

    const int ir = selecionar("REMOVER PEÇA DA MÁQUINA", relOpcoes);
    if (ir < 0) return;

    const auto& rel = relacoes[static_cast<size_t>(ir)];
    if (!confirmar("CONFIRMAR REMOÇÃO", "Remover esta peça da máquina?")) return;

    const Resultado res = sistema.maquinas.removerRelacao(idMaquina, rel.idPeca);
    pausarMensagem(res.ok ? "Sucesso" : "Erro", res.mensagem, !res.ok);
}

void menuMaquinas(Sistema& sistema) {
    const vector<string> opcoes = {
        "Cadastrar Máquina",
        "Visualizar Máquinas",
        "Editar Máquina",
        "Remover Máquina",
        "Adicionar Peça à Máquina",
        "Visualizar Peças por Máquina",
        "Alterar Quantidade Necessária",
        "Remover Peça da Máquina",
        "Voltar"
    };

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo("MÁQUINAS"),
                     card("MÁQUINAS", to_string(sistema.maquinas.lista().size()), Color::Cyan),
                     separator(),
                     menu->Render() | border | flex,
                     separator(),
                     text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: cadastrarMaquina(sistema); break;
                case 1: visualizarMaquina(sistema); break;
                case 2: editarMaquina(sistema); break;
                case 3: removerMaquina(sistema); break;
                case 4: relacionarPecaMaquina(sistema); break;
                case 5: visualizarMaquina(sistema); break;
                case 6: editarRelacionamento(sistema); break;
                case 7: removerRelacionamento(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

// ============================================================================
// UI — MANUTENÇÃO
// ============================================================================
int selecionarMaquina(const Sistema& sistema, const string& tituloSelecao) {
    const auto& maquinas = sistema.maquinas.lista();
    vector<string> opcoes;
    for (const auto& m : maquinas)
        opcoes.push_back(m.nome);
    return selecionar(tituloSelecao, opcoes);
}

void registrarManutencao(Sistema& sistema) {
    const auto& maquinas = sistema.maquinas.lista();
    if (maquinas.empty()) {
        pausarMensagem("Manutenção", "Cadastre uma máquina antes de registrar uma manutenção.", true);
        return;
    }

    const int im = selecionarMaquina(sistema, "SELECIONE A MÁQUINA");
    if (im < 0) return;

    const int idMaquina = maquinas[static_cast<size_t>(im)].id;

    auto base = formularioSimples(
        "REGISTRAR MANUTENÇÃO",
        {{"Data DD/MM/AAAA", dataHoje()},
         {"Observação", ""}});
    if (!base.confirmado) return;

    Manutencao manutencao;
    manutencao.idMaquina = idMaquina;
    manutencao.data = base.valores[0];
    manutencao.observacao = base.valores[1];

    // A lista contém apenas peças que possuem saldo positivo no estoque global.
    while (true) {
        const auto estoqueAtual = sistema.estoque.listar();
        vector<VisaoEstoque> pecasDisponiveis;
        for (const auto& p : estoqueAtual) {
            int quantidadeJaInformada = 0;
            for (const auto& usada : manutencao.pecas) {
                if (usada.idPeca == p.peca.id) quantidadeJaInformada += usada.quantidade;
            }
            if (p.quantidade - quantidadeJaInformada > 0) {
                VisaoEstoque disponivel = p;
                disponivel.quantidade = p.quantidade - quantidadeJaInformada;
                pecasDisponiveis.push_back(disponivel);
            }
        }

        vector<string> opcoes;
        for (const auto& p : pecasDisponiveis) {
            opcoes.push_back(p.peca.nome +
                             " | Disponível para esta manutenção: " + to_string(p.quantidade));
        }
        opcoes.push_back("FINALIZAR PEÇAS");

        const int ip = selecionar("PEÇAS EM ESTOQUE PARA UTILIZAR", opcoes);
        if (ip < 0 || ip == static_cast<int>(pecasDisponiveis.size())) break;

        const auto& escolhida = pecasDisponiveis[static_cast<size_t>(ip)];
        const int idPeca = escolhida.peca.id;
        const string nomePeca = escolhida.peca.nome;
        const int disponivel = escolhida.quantidade;

        auto r = formularioSimples("QUANTIDADE UTILIZADA",
                                   {{nomePeca + " (disponível: " + to_string(disponivel) + ")", "1"}},
                                   "Adicionar");
        if (!r.confirmado) break;

        int quantidade = 0;
        if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0 || quantidade > disponivel) {
            pausarMensagem("Erro", "Informe uma quantidade maior que zero e que não ultrapasse o estoque disponível.", true);
            continue;
        }

        manutencao.pecas.push_back({idPeca, quantidade});
        if (!confirmar("ADICIONAR OUTRA PEÇA", "Deseja informar outra peça utilizada?")) break;
    }

    const Resultado res = sistema.manutencao.registrar(manutencao);
    pausarMensagem(res.ok ? "Sucesso" : "Manutenção não registrada",
                   res.mensagem, !res.ok);
}

void pecasUtilizadasManutencao(Sistema& sistema) {
    const auto& historico = sistema.manutencao.historico();
    if (historico.empty()) {
        pausarMensagem("Manutenção", "Nenhuma manutenção registrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& m : historico) {
        const Maquina* maquina = sistema.maquinas.maquina(m.idMaquina);
        opcoes.push_back("#" + to_string(m.id) +
                         " | " + (maquina ? maquina->nome : "Máquina não encontrada") +
                         " | " + m.data);
    }

    const int indice = selecionar("PEÇAS UTILIZADAS — MANUTENÇÃO", opcoes);
    if (indice < 0) return;

    const auto& m = historico[static_cast<size_t>(indice)];
    string mensagem = "Manutenção #" + to_string(m.id) + "\n";
    mensagem += "Data: " + m.data + "\n";
    mensagem += "Peças utilizadas:\n";

    if (m.pecas.empty()) {
        mensagem += "Nenhuma peça.\n";
    } else {
        for (const auto& usada : m.pecas) {
            const Peca* p = sistema.estoque.peca(usada.idPeca);
            mensagem += "- " + string(p ? p->nome : "Peça não encontrada") +
                        " | Quantidade: " + to_string(usada.quantidade) + "\n";
        }
    }

    pausarMensagem("PEÇAS UTILIZADAS", mensagem);
}

void historicoManutencao(Sistema& sistema) {
    const auto& historico = sistema.manutencao.historico();
    if (historico.empty()) {
        pausarMensagem("Histórico", "Nenhuma manutenção registrada.");
        return;
    }

    vector<string> opcoes;
    for (const auto& m : historico) {
        const Maquina* maquina = sistema.maquinas.maquina(m.idMaquina);
        opcoes.push_back("#" + to_string(m.id) +
                         " | " + m.data +
                         " | " + (maquina ? maquina->nome : "Máquina não encontrada") +
                         (m.observacao.empty() ? "" : " | " + m.observacao));
    }

    const int indice = selecionar("HISTÓRICO DE MANUTENÇÕES", opcoes);
    if (indice < 0) return;

    const auto& m = historico[static_cast<size_t>(indice)];
    const Maquina* maquina = sistema.maquinas.maquina(m.idMaquina);

    string mensagem =
        "ID da manutenção: " + to_string(m.id) +
        "\nMáquina: " + (maquina ? maquina->nome : "não encontrada") +
        "\nData: " + m.data + "\n";

    if (!m.observacao.empty()) mensagem += "Observação: " + m.observacao + "\n";
    mensagem += "\nPeças utilizadas:\n";

    if (m.pecas.empty()) {
        mensagem += "Nenhuma\n";
    } else {
        for (const auto& usada : m.pecas) {
            const Peca* p = sistema.estoque.peca(usada.idPeca);
            mensagem += "- " + string(p ? p->nome : "Peça não encontrada") +
                        ": " + to_string(usada.quantidade) + "\n";
        }
    }

    pausarMensagem("DETALHES DA MANUTENÇÃO", mensagem);
}

void menuManutencao(Sistema& sistema) {
    const vector<string> opcoes = {
        "Registrar Manutenção",
        "Peças Utilizadas",
        "Histórico de Manutenção",
        "Voltar"
    };

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo("MANUTENÇÃO"),
                     separator(),
                     menu->Render() | border | flex,
                     separator(),
                     text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: registrarManutencao(sistema); break;
                case 1: pecasUtilizadasManutencao(sistema); break;
                case 2: historicoManutencao(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

// ============================================================================
// UI — VENDAS, CONSULTAS E RANKINGS
// ============================================================================
vector<string> clientesDasVendas(const vector<Venda>& vendas) {
    vector<string> resultado;
    for (const auto& v : vendas) {
        if (find_if(resultado.begin(), resultado.end(), [&](const string& c) {
                return mesmoTexto(c, v.cliente);
            }) == resultado.end()) {
            resultado.push_back(v.cliente);
        }
    }
    sort(resultado.begin(), resultado.end(), [](const string& a, const string& b) {
        return minusculas(a) < minusculas(b);
    });
    return resultado;
}

void mostrarVendas(const vector<Venda>& vendas, const string& tituloVendas) {
    if (vendas.empty()) {
        pausarMensagem(tituloVendas, "Nenhuma venda encontrada.");
        return;
    }

    vector<string> linhas;
    for (size_t i = 0; i < vendas.size(); ++i) {
        const Venda& v = vendas[i];
        if (v.registroAntigoDePecas) {
            linhas.push_back(to_string(i + 1) + ". Registro anterior | Cliente: " + v.cliente +
                             " | Máquina: " + v.nomeMaquina +
                             " | Quantidade histórica: " + to_string(v.quantidade));
        } else {
            linhas.push_back(to_string(i + 1) + ". Cliente: " + v.cliente +
                             " | Máquina: " + v.nomeMaquina +
                             " | Máquinas vendidas: " + to_string(v.quantidade));
        }
    }

    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo(tituloVendas),
                     menu->Render() | frame | vscroll_indicator | border | flex,
                     separator(),
                     text("Enter = detalhes | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const Venda& v = vendas[static_cast<size_t>(selecionado)];
            string mensagem;
            if (v.registroAntigoDePecas) {
                mensagem = "Registro de venda anterior à alteração do sistema.\n";
                mensagem += "Cliente: " + v.cliente + "\n";
                mensagem += "Máquina associada: " + v.nomeMaquina + "\n";
                mensagem += "Quantidade histórica (não contabilizada como máquinas): " + to_string(v.quantidade);
            } else {
                mensagem = "Cliente: " + v.cliente +
                           "\nMáquina: " + v.nomeMaquina +
                           "\nQuantidade de máquinas vendidas: " + to_string(v.quantidade);
            }
            pausarMensagem("DETALHES DA VENDA", mensagem);
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

void registrarVenda(Sistema& sistema) {
    if (sistema.maquinas.lista().empty()) {
        pausarMensagem("Venda", "Cadastre uma máquina antes de registrar uma venda.", true);
        return;
    }
    auto cliente = formularioSimples("CLIENTE", {{"Nome do cliente", ""}}, "Continuar");
    if (!cliente.confirmado) return;

    const int im = selecionarMaquina(sistema, "MÁQUINA VENDIDA");
    if (im < 0) return;
    const int idMaquina = sistema.maquinas.lista()[static_cast<size_t>(im)].id;

    auto quantidade = formularioSimples("QUANTIDADE DE MÁQUINAS VENDIDAS", {{"Quantidade de máquinas", "1"}});
    if (!quantidade.confirmado) return;

    int qtd = 0;
    if (!inteiroValido(quantidade.valores[0], qtd) || qtd <= 0) {
        pausarMensagem("Erro", "Quantidade inválida.", true);
        return;
    }

    const Resultado res = sistema.vendas.registrar(idMaquina, qtd, cliente.valores[0]);
    pausarMensagem(res.ok ? "Venda registrada" : "Venda não registrada",
                   res.mensagem, !res.ok);
}

void consultarVendas(Sistema& sistema) {
    const vector<string> opcoes = {
        "Todas as vendas",
        "Filtrar por cliente",
        "Filtrar por máquina",
        "Voltar"
    };

    const int escolha = selecionar("CONSULTAR VENDAS", opcoes);
    if (escolha < 0 || escolha == 3) return;

    if (escolha == 0) {
        mostrarVendas(sistema.vendas.consultar(), "TODAS AS VENDAS");
        return;
    }

    if (escolha == 1) {
        const auto clientes = clientesDasVendas(sistema.vendas.lista());
        const int indice = selecionar("SELECIONE O CLIENTE", clientes);
        if (indice < 0) return;
        mostrarVendas(sistema.vendas.consultar(clientes[static_cast<size_t>(indice)]),
                      "VENDAS DO CLIENTE");
        return;
    }

    if (escolha == 2) {
        const int indice = selecionarMaquina(sistema, "SELECIONE A MÁQUINA");
        if (indice < 0) return;
        const int idMaquina = sistema.maquinas.lista()[static_cast<size_t>(indice)].id;
        mostrarVendas(sistema.vendas.consultar("", idMaquina), "VENDAS DA MÁQUINA");
        return;
    }
}

void historicoVendas(Sistema& sistema) {
    mostrarVendas(sistema.vendas.consultar(), "HISTÓRICO DE VENDAS");
}

void rankingClientes(Sistema& sistema) {
    const auto ranking = sistema.vendas.rankingClientes();
    if (ranking.empty()) {
        pausarMensagem("Ranking de Clientes", "Nenhuma venda registrada.");
        return;
    }

    string mensagem = "RANKING DE CLIENTES\n\n";
    for (size_t i = 0; i < ranking.size(); ++i) {
        mensagem += to_string(i + 1) + "º - " + ranking[i].nome +
                    "     " + to_string(ranking[i].total) + " máquinas vendidas\n";
    }

    pausarMensagem("RANKING DE CLIENTES", mensagem);
}

void rankingMaquinas(Sistema& sistema) {
    const auto ranking = sistema.vendas.rankingMaquinas();
    if (ranking.empty()) {
        pausarMensagem("Ranking de Máquinas", "Nenhuma venda associada a uma máquina foi registrada.");
        return;
    }

    // Ranking por quantidade de máquinas vendidas de cada máquina cadastrada.
    string mensagem = "RANKING DE MÁQUINAS\n\n";
    for (size_t i = 0; i < ranking.size(); ++i) {
        mensagem += to_string(i + 1) + "º - " + ranking[i].nome +
                    "     " + to_string(ranking[i].total) + " máquinas vendidas\n";
    }

    pausarMensagem("RANKING DE MÁQUINAS", mensagem);
}

void menuVendas(Sistema& sistema) {
    const vector<string> opcoes = {
        "Registrar Venda",
        "Histórico de Vendas",
        "Ver Vendas",
        "Ranking de Clientes",
        "Ranking de Máquinas",
        "Voltar"
    };

    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(menu, [&] {
        return vbox({titulo("VENDAS"),
                     hbox({card("REGISTROS DE VENDA", to_string(sistema.vendas.lista().size()), Color::Green),
                           card("MÁQUINAS VENDIDAS", to_string(sistema.vendas.totalMaquinasVendidas()), Color::Cyan),
                           card("MÁQUINAS CADASTRADAS", to_string(sistema.maquinas.lista().size()), Color::Yellow)}),
                     separator(),
                     menu->Render() | border | flex,
                     separator(),
                     text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape) {
            tela.Exit();
            return true;
        }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: registrarVenda(sistema); break;
                case 1: historicoVendas(sistema); break;
                case 2: consultarVendas(sistema); break;
                case 3: rankingClientes(sistema); break;
                case 4: rankingMaquinas(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

// ============================================================================
// UI — DASHBOARD / MAIN
// ============================================================================
void dashboard(Sistema& sistema) {
    auto voltar = Button("Voltar", [] {});
    auto tela = ScreenInteractive::Fullscreen();

    auto app = Renderer(voltar, [&] {
        const int criticos = sistema.estoque.totalCriticos();
        const size_t maquinas = sistema.maquinas.lista().size();
        const size_t pecas = sistema.estoque.totalPecas();
        const size_t vendas = sistema.vendas.lista().size();

        return vbox({titulo("PAINEL PRINCIPAL"),
                     text("Controle de peças, máquinas, vendas e manutenção") |
                         center | color(Color::GrayLight),
                     separator(),
                     hbox({card("PEÇAS", to_string(pecas), Color::Cyan),
                           card("MÁQUINAS", to_string(maquinas), Color::Yellow),
                           card("CRÍTICOS", to_string(criticos), Color::Red),
                           card("VENDAS", to_string(vendas), Color::Green)}),
                     separator(),
                     text(criticos > 0
                              ? "ATENÇÃO: existem peças abaixo do estoque mínimo."
                              : "Estoque sem itens críticos.") |
                         center |
                         (criticos > 0 ? color(Color::Red) : color(Color::Green)),
                     separator(),
                     voltar->Render() | center}) |
               border;
    });

    app = CatchEvent(app, [&](Event event) {
        if (event == Event::Escape || event == Event::Return) {
            tela.Exit();
            return true;
        }
        return false;
    });

    tela.Loop(app);
}

}  // namespace ui

// ============================================================================
// MAIN
// ============================================================================
int main() {
    Sistema sistema;

    if (!sistema.carregar()) {
        ui::pausarMensagem(
            "AVISO",
            "Alguns arquivos de dados não puderam ser lidos. Os registros válidos foram carregados.",
            true);
    }

    while (true) {
        const vector<string> opcoes = {
            "Painel Principal",
            "Máquinas",
            "Peças / Estoque",
            "Manutenção",
            "Vendas",
            "Salvar Dados",
            "Sair"
        };

        int selecionado = 0;
        auto menu = Menu(&opcoes, &selecionado);
        auto tela = ScreenInteractive::Fullscreen();
        int acao = -1;

        auto app = Renderer(menu, [&] {
            const auto itensParaComprar = sistema.estoque.criticos();

            Elements linhasCompras;
            if (itensParaComprar.empty()) {
                linhasCompras.push_back(
                    text("Nenhuma peça precisa ser comprada. Estoque dentro do limite mínimo.") |
                    center | color(Color::Green));
            } else {
                for (const auto& item : itensParaComprar) {
                    linhasCompras.push_back(
                        hbox({
                            text("• " + item.peca.nome) | flex,
                            text("Estoque: " + to_string(item.quantidade) +
                                 " | Comprar: " + to_string(item.comprar))
                        }));
                }
            }

            return vbox({
                text("GERENCIAMENTO DE ESTOQUE, PEÇAS, VENDAS E MANUTENÇÃO") |
                    bold | color(Color::Cyan) | center,
                text("MÁQUINAS DE BENEFICIAMENTO DE CAFÉ") |
                    bold | color(Color::Yellow) | center,
                separator(),
                hbox({
                    ui::card("Peças", to_string(sistema.estoque.totalPecas()), Color::Cyan),
                    ui::card("Máquinas", to_string(sistema.maquinas.lista().size()), Color::Yellow),
                    ui::card("Críticos", to_string(sistema.estoque.totalCriticos()), Color::Red),
                    ui::card("A COMPRAR", to_string(itensParaComprar.size()), Color::Magenta),
                    ui::card("Vendas", to_string(sistema.vendas.lista().size()), Color::Green)
                }),
                separator(),
                window(
                    text(" O QUE PRECISA COMPRAR DO ESTOQUE ") | bold | color(Color::Yellow),
                    vbox(std::move(linhasCompras))
                ) | flex,
                separator(),
                menu->Render() | border | flex,
                separator(),
                text("Setas/Tab = navegar | Enter = abrir | Esc = sair") |
                    color(Color::GrayDark) | center
            }) |
            border | bgcolor(Color::RGB(10, 15, 25));
        });

        app = CatchEvent(app, [&](Event event) {
            if (event == Event::Escape) {
                acao = 6;
                tela.Exit();
                return true;
            }
            if (event == Event::Return) {
                acao = selecionado;
                tela.Exit();
                return true;
            }
            return false;
        });

        tela.Loop(app);

        switch (acao) {
            case 0:
                ui::dashboard(sistema);
                break;
            case 1:
                ui::menuMaquinas(sistema);
                break;
            case 2:
                ui::menuEstoque(sistema);
                break;
            case 3:
                ui::menuManutencao(sistema);
                break;
            case 4:
                ui::menuVendas(sistema);
                break;
            case 5:
                if (sistema.salvar())
                    ui::pausarMensagem("Sucesso", "Todos os dados foram salvos com sucesso.");
                else
                    ui::pausarMensagem("Erro", "Falha ao salvar alguns dados.", true);
                break;
            case 6:
                if (!sistema.salvar()) {
                    ui::pausarMensagem(
                        "Erro ao salvar",
                        "Alguns dados não puderam ser gravados. O sistema não será encerrado para evitar perda de dados.",
                        true);
                } else {
                    cout << "\nSistema encerrado pelo usuário.\n";
                    return 0;
                }
                break;
            default:
                break;
        }
    }
}
