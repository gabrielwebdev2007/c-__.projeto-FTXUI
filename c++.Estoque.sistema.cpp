#include <ftxui/ftxui.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ftxui;
using namespace std;

namespace config {
constexpr int LIMITE_ESTOQUE_BAIXO = 10;
const string ARQ_ESTOQUE = "estoque.txt";
const string ARQ_VENDAS = "vendas_semana.txt";
const string ARQ_DEVOLUCOES = "devolucoes.txt";
}

struct ItemEstoque {
    string nome;
    int quantidade = 0;
    string observacao;
};

struct Venda {
    string item;
    int quantidade = 0;
    string cliente;
};

struct Devolucao {
    string item;
    int quantidade = 0;
    string cliente;
};

struct ClienteRanking {
    string nome;
    int totalComprado = 0;
};

struct ItemRanking {
    string nome;
    int totalVendido = 0;
};

struct ResultadoFormulario {
    bool confirmado = false;
    vector<string> valores;
};

// ============================================================
// Utilidades
// ============================================================
string aparar(const string& texto) {
    const string espacos = " \t\r\n";
    const size_t inicio = texto.find_first_not_of(espacos);
    if (inicio == string::npos) return "";
    const size_t fim = texto.find_last_not_of(espacos);
    return texto.substr(inicio, fim - inicio + 1);
}

string limparSeparador(string texto) {
    for (char& c : texto) {
        if (c == '|') c = '/';
    }
    return texto;
}

vector<string> separar(const string& linha, char delimitador) {
    vector<string> partes;
    string parte;
    stringstream ss(linha);
    while (getline(ss, parte, delimitador)) partes.push_back(parte);
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

// ============================================================
// Persistência
// ============================================================
class Repositorio {
public:
    bool carregarEstoque(vector<ItemEstoque>& estoque) const {
        estoque.clear();
        ifstream arquivo(config::ARQ_ESTOQUE);
        if (!arquivo.is_open()) return true;

        string linha;
        while (getline(arquivo, linha)) {
            if (aparar(linha).empty()) continue;
            const auto campos = separar(linha, '|');
            if (campos.size() < 2) continue;

            int quantidade = 0;
            if (!inteiroValido(campos[1], quantidade) || quantidade < 0) continue;

            const string nome = aparar(campos[0]);
            if (nome.empty()) continue;

            ItemEstoque item{nome, quantidade, campos.size() >= 3 ? aparar(campos[2]) : ""};
            estoque.push_back(item);
        }
        return !arquivo.bad();
    }

    bool salvarEstoque(const vector<ItemEstoque>& estoque) const {
        return salvar(config::ARQ_ESTOQUE, [&](ofstream& arquivo) {
            for (const auto& item : estoque) {
                arquivo << limparSeparador(item.nome) << '|'
                        << item.quantidade << '|'
                        << limparSeparador(item.observacao) << '\n';
            }
        });
    }

    bool carregarVendas(vector<Venda>& vendas) const {
        vendas.clear();
        ifstream arquivo(config::ARQ_VENDAS);
        if (!arquivo.is_open()) return true;

        string linha;
        while (getline(arquivo, linha)) {
            if (aparar(linha).empty()) continue;
            const auto campos = separar(linha, '|');
            if (campos.size() < 3) continue;

            int quantidade = 0;
            if (!inteiroValido(campos[1], quantidade) || quantidade <= 0) continue;

            const string item = aparar(campos[0]);
            const string cliente = aparar(campos[2]);
            if (item.empty() || cliente.empty()) continue;

            vendas.push_back({item, quantidade, cliente});
        }
        return !arquivo.bad();
    }

    bool salvarVendas(const vector<Venda>& vendas) const {
        return salvar(config::ARQ_VENDAS, [&](ofstream& arquivo) {
            for (const auto& venda : vendas) {
                arquivo << limparSeparador(venda.item) << '|'
                        << venda.quantidade << '|'
                        << limparSeparador(venda.cliente) << '\n';
            }
        });
    }

    bool carregarDevolucoes(vector<Devolucao>& devolucoes) const {
        devolucoes.clear();
        ifstream arquivo(config::ARQ_DEVOLUCOES);
        if (!arquivo.is_open()) return true;

        string linha;
        while (getline(arquivo, linha)) {
            if (aparar(linha).empty()) continue;
            const auto campos = separar(linha, '|');
            if (campos.size() < 3) continue;

            int quantidade = 0;
            if (!inteiroValido(campos[1], quantidade) || quantidade <= 0) continue;

            const string item = aparar(campos[0]);
            const string cliente = aparar(campos[2]);
            if (item.empty() || cliente.empty()) continue;

            devolucoes.push_back({item, quantidade, cliente});
        }
        return !arquivo.bad();
    }

    bool salvarDevolucoes(const vector<Devolucao>& devolucoes) const {
        return salvar(config::ARQ_DEVOLUCOES, [&](ofstream& arquivo) {
            for (const auto& devolucao : devolucoes) {
                arquivo << limparSeparador(devolucao.item) << '|'
                        << devolucao.quantidade << '|'
                        << limparSeparador(devolucao.cliente) << '\n';
            }
        });
    }

private:
    template <typename Funcao>
    bool salvar(const string& caminho, Funcao escrever) const {
        ofstream arquivo(caminho, ios::out | ios::trunc);
        if (!arquivo.is_open()) return false;
        escrever(arquivo);
        return arquivo.good();
    }
};

// ============================================================
// Regras de negócio / estado do sistema
// ============================================================
class SistemaEstoque {
public:
    bool carregarTudo() {
        return repositorio.carregarEstoque(estoque) &&
               repositorio.carregarVendas(vendas) &&
               repositorio.carregarDevolucoes(devolucoes);
    }

    bool salvarTudo() const {
        const bool estoqueOk = repositorio.salvarEstoque(estoque);
        const bool vendasOk = repositorio.salvarVendas(vendas);
        const bool devolucoesOk = repositorio.salvarDevolucoes(devolucoes);
        return estoqueOk && vendasOk && devolucoesOk;
    }

    bool salvarApenasEstoque() const {
        return repositorio.salvarEstoque(estoque);
    }

    int encontrarItem(const string& nome) const {
        const string alvo = aparar(nome);
        for (size_t i = 0; i < estoque.size(); ++i) {
            if (estoque[i].nome == alvo) return static_cast<int>(i);
        }
        return -1;
    }

    bool adicionarItem(const string& nome, int quantidade, const string& observacao, string& erro) {
        const string nomeLimpo = aparar(nome);
        if (nomeLimpo.empty()) { erro = "Informe o nome do item."; return false; }
        if (quantidade < 0) { erro = "A quantidade não pode ser negativa."; return false; }

        const int pos = encontrarItem(nomeLimpo);
        if (pos >= 0) {
            estoque[pos].quantidade += quantidade;
            const string obs = aparar(observacao);
            if (!obs.empty()) estoque[pos].observacao = obs;
        } else {
            estoque.push_back({nomeLimpo, quantidade, aparar(observacao)});
        }
        return repositorio.salvarEstoque(estoque) ? true : (erro = "Não foi possível gravar o estoque.", false);
    }

    bool editarItem(size_t indice, int quantidade, const string& observacao, string& erro) {
        if (indice >= estoque.size()) { erro = "Item inválido."; return false; }
        if (quantidade < 0) { erro = "A quantidade não pode ser negativa."; return false; }
        estoque[indice].quantidade = quantidade;
        estoque[indice].observacao = aparar(observacao);
        return repositorio.salvarEstoque(estoque) ? true : (erro = "Não foi possível gravar o estoque.", false);
    }

    bool removerItem(size_t indice, string& nomeRemovido, string& erro) {
        if (indice >= estoque.size()) {
            erro = "Item inválido.";
            return false;
        }

        // Guarda o item para permitir restauração se o arquivo não puder ser salvo.
        const ItemEstoque itemRemovido = estoque[indice];
        nomeRemovido = itemRemovido.nome;

        estoque.erase(estoque.begin() + static_cast<ptrdiff_t>(indice));

        if (!repositorio.salvarEstoque(estoque)) {
            // Se falhar ao salvar, desfaz a remoção em memória.
            estoque.insert(estoque.begin() + static_cast<ptrdiff_t>(indice), itemRemovido);
            erro = "Não foi possível gravar o estoque. O item não foi removido.";
            return false;
        }

        return true;
    }

    bool registrarVenda(size_t indice, int quantidade, const string& cliente, string& erro) {
        if (indice >= estoque.size()) { erro = "Item não encontrado no estoque."; return false; }
        if (quantidade <= 0) { erro = "A quantidade vendida deve ser maior que zero."; return false; }
        const string clienteLimpo = aparar(cliente);
        if (clienteLimpo.empty()) { erro = "Informe o nome do cliente."; return false; }

        const int vendida = min(quantidade, estoque[indice].quantidade);
        estoque[indice].quantidade -= vendida;
        vendas.push_back({estoque[indice].nome, quantidade, clienteLimpo});

        if (!repositorio.salvarEstoque(estoque) || !repositorio.salvarVendas(vendas)) {
            erro = "Não foi possível salvar a venda e o estoque.";
            return false;
        }
        return true;
    }

    bool editarVenda(size_t indice, int quantidade, const string& cliente, string& erro) {
        if (indice >= vendas.size()) { erro = "Venda inválida."; return false; }
        if (quantidade <= 0 || aparar(cliente).empty()) { erro = "Preencha os dados corretamente."; return false; }
        vendas[indice].quantidade = quantidade;
        vendas[indice].cliente = aparar(cliente);
        return repositorio.salvarVendas(vendas) ? true : (erro = "Não foi possível gravar as vendas.", false);
    }

    bool removerVenda(size_t indice, string& erro) {
        if (indice >= vendas.size()) { erro = "Venda inválida."; return false; }
        vendas.erase(vendas.begin() + static_cast<ptrdiff_t>(indice));
        return repositorio.salvarVendas(vendas) ? true : (erro = "Não foi possível gravar as vendas.", false);
    }

    bool renomearCliente(const string& antigo, const string& novo, string& erro) {
        const string novoLimpo = aparar(novo);
        if (novoLimpo.empty()) { erro = "Informe um nome válido."; return false; }
        for (auto& venda : vendas) if (venda.cliente == antigo) venda.cliente = novoLimpo;
        return repositorio.salvarVendas(vendas) ? true : (erro = "Não foi possível gravar as vendas.", false);
    }

    bool registrarDevolucao(const string& item, int quantidade, const string& cliente, string& erro) {
        const string itemLimpo = aparar(item);
        const string clienteLimpo = aparar(cliente);
        if (itemLimpo.empty() || clienteLimpo.empty()) { erro = "Informe item e cliente."; return false; }
        if (quantidade <= 0) { erro = "A quantidade deve ser maior que zero."; return false; }

        const int pos = encontrarItem(itemLimpo);
        if (pos >= 0) estoque[pos].quantidade += quantidade;
        else estoque.push_back({itemLimpo, quantidade, "Criado automaticamente pela devolução."});

        devolucoes.push_back({itemLimpo, quantidade, clienteLimpo});
        if (!repositorio.salvarEstoque(estoque) || !repositorio.salvarDevolucoes(devolucoes)) {
            erro = "Não foi possível salvar a devolução e o estoque.";
            return false;
        }
        return true;
    }

    bool editarDevolucao(size_t indice, const string& novoItem, int novaQuantidade,
                         const string& novoCliente, string& erro) {
        if (indice >= devolucoes.size()) { erro = "Devolução inválida."; return false; }
        const string itemLimpo = aparar(novoItem);
        const string clienteLimpo = aparar(novoCliente);
        if (itemLimpo.empty() || clienteLimpo.empty() || novaQuantidade <= 0) {
            erro = "Preencha os dados corretamente.";
            return false;
        }

        const Devolucao antiga = devolucoes[indice];
        const int posAntiga = encontrarItem(antiga.item);
        if (posAntiga >= 0) {
            estoque[posAntiga].quantidade = max(0, estoque[posAntiga].quantidade - antiga.quantidade);
        }

        const int posNova = encontrarItem(itemLimpo);
        if (posNova >= 0) estoque[posNova].quantidade += novaQuantidade;
        else estoque.push_back({itemLimpo, novaQuantidade, "Criado pela devolução editada."});

        devolucoes[indice] = {itemLimpo, novaQuantidade, clienteLimpo};
        if (!repositorio.salvarEstoque(estoque) || !repositorio.salvarDevolucoes(devolucoes)) {
            erro = "Não foi possível salvar a devolução e o estoque.";
            return false;
        }
        return true;
    }

    vector<ItemRanking> rankingItens() const {
        unordered_map<string, int> totais;
        for (const auto& venda : vendas) totais[venda.item] += venda.quantidade;
        vector<ItemRanking> ranking;
        ranking.reserve(totais.size());
        for (const auto& [nome, total] : totais) ranking.push_back({nome, total});
        sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) {
            return a.totalVendido > b.totalVendido;
        });
        return ranking;
    }

    vector<ClienteRanking> rankingClientes() const {
        unordered_map<string, int> totais;
        for (const auto& venda : vendas) totais[venda.cliente] += venda.quantidade;
        vector<ClienteRanking> ranking;
        ranking.reserve(totais.size());
        for (const auto& [nome, total] : totais) ranking.push_back({nome, total});
        sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) {
            return a.totalComprado > b.totalComprado;
        });
        return ranking;
    }

    int quantidadeItensBaixos() const {
        return static_cast<int>(count_if(estoque.begin(), estoque.end(), [](const auto& item) {
            return item.quantidade < config::LIMITE_ESTOQUE_BAIXO;
        }));
    }

    int totalUnidades() const {
        int total = 0;
        for (const auto& item : estoque) total += item.quantidade;
        return total;
    }

    const vector<ItemEstoque>& getEstoque() const { return estoque; }
    const vector<Venda>& getVendas() const { return vendas; }
    const vector<Devolucao>& getDevolucoes() const { return devolucoes; }

private:
    Repositorio repositorio;
    vector<ItemEstoque> estoque;
    vector<Venda> vendas;
    vector<Devolucao> devolucoes;
};

// ============================================================
// Interface FTXUI
// ============================================================
Element titulo(const string& texto) {
    return window(text("  " + texto + "  ") | bold | color(Color::White), text("")) |
           bgcolor(Color::RGB(35, 45, 65));
}

Element card(const string& cabecalho, const string& valor, Color cor) {
    return window(text(" " + cabecalho + " ") | bold | color(cor),
                  text("  " + valor + "  ") | bold | center) | flex;
}

void mensagem(const string& cabecalho, const string& texto, bool erro = false) {
    auto ok = Button("OK", [] {});
    auto tela = ScreenInteractive::Fullscreen();
    auto componente = Renderer(ok, [&] {
        return window(text(" " + cabecalho + " ") | bold | color(erro ? Color::Red : Color::Green),
                      vbox({paragraph(texto) | center, separator(), ok->Render() | center})) |
               size(WIDTH, GREATER_THAN, 60) | center;
    });
    componente = CatchEvent(componente, [&](Event event) {
        if (event == Event::Return || event == Event::Escape) {
            tela.Exit();
            return true;
        }
        return false;
    });
    tela.Loop(componente);
}

bool confirmarAcao(const string& cabecalho, const string& pergunta) {
    bool resposta = false;
    bool decidido = false;

    auto tela = ScreenInteractive::Fullscreen();

    auto sim = Button("SIM", [&] {
        resposta = true;
        decidido = true;
        tela.Exit();
    });

    auto nao = Button("NAO", [&] {
        resposta = false;
        decidido = true;
        tela.Exit();
    });

    auto botoes = Container::Horizontal({sim, nao});

    auto componente = Renderer(botoes, [&] {
        return window(
            text(" " + cabecalho + " ") | bold | color(Color::Red),
            vbox({
                paragraph(pergunta) | center,
                separator(),
                text("Escolha uma opção:") | bold | center,
                separator(),
                hbox({
                    sim->Render() | center,
                    nao->Render() | center
                }) | center
            })
        ) | size(WIDTH, GREATER_THAN, 65) | center;
    });

    componente = CatchEvent(componente, [&](Event event) {
        if (event == Event::Escape) {
            resposta = false;
            decidido = true;
            tela.Exit();
            return true;
        }

        return false;
    });

    tela.Loop(componente);
    return decidido && resposta;
}

ResultadoFormulario formulario(const string& tituloFormulario,
                               const vector<pair<string, string>>& campos,
                               const string& textoBotao = "Salvar") {
    vector<string> valores;
    vector<Component> inputs;
    valores.reserve(campos.size());
    inputs.reserve(campos.size());

    for (const auto& campo : campos) {
        valores.push_back(campo.second);
        inputs.push_back(Input(&valores.back(), campo.first));
    }

    auto container = Container::Vertical(inputs);
    auto salvar = Button(textoBotao, [] {});
    auto cancelar = Button("Cancelar", [] {});
    auto botoes = Container::Horizontal({salvar, cancelar});
    auto raiz = Container::Vertical({container, botoes});

    bool confirmado = false;
    auto renderer = Renderer(raiz, [&] {
        Elements linhas;
        for (size_t i = 0; i < valores.size(); ++i) {
            linhas.push_back(hbox({text(campos[i].first + ": ") | size(WIDTH, EQUAL, 22),
                                   inputs[i]->Render() | flex}) |
                             border | size(HEIGHT, EQUAL, 3));
        }
        return window(text(" " + tituloFormulario + " ") | bold | color(Color::Cyan),
                      vbox({vbox(std::move(linhas)), separator(), botoes->Render() | center})) |
               size(WIDTH, GREATER_THAN, 65) | center;
    });

    auto tela = ScreenInteractive::Fullscreen();
    auto app = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) { confirmado = true; tela.Exit(); return true; }
        return false;
    });
    tela.Loop(app);
    return {confirmado, valores};
}

void executarTela(const string& nome, const Component& componente) {
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(componente, [&] {
        return vbox({text("SISTEMA DE ESTOQUE") | bold | color(Color::Cyan) | center,
                      text(nome) | bold | color(Color::Yellow) | center,
                      separator(), componente->Render() | flex, separator(),
                      text("Setas/Tab = navegar | Enter = selecionar | Esc = voltar") |
                          color(Color::GrayDark) | center}) |
               border | bgcolor(Color::RGB(15, 20, 30));
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        return false;
    });
    tela.Loop(renderer);
}

// ============================================================
// Telas de estoque
// ============================================================
void cadastrarItem(SistemaEstoque& sistema) {
    const auto r = formulario("ADICIONAR ITEM AO ESTOQUE", {
        {"Nome do item", ""}, {"Quantidade", "0"}, {"Observação/Nota", ""}
    });
    if (!r.confirmado) return;

    int quantidade = 0;
    if (!inteiroValido(r.valores[1], quantidade) || quantidade < 0) {
        mensagem("Erro", "A quantidade deve ser um número inteiro maior ou igual a zero.", true);
        return;
    }
    string erro;
    if (!sistema.adicionarItem(r.valores[0], quantidade, r.valores[2], erro)) {
        mensagem("Erro", erro, true);
        return;
    }
    mensagem("Salvo com sucesso", "Item adicionado em estoque.");
}

void listarEstoque(SistemaEstoque& sistema) {
    const auto& estoque = sistema.getEstoque();
    if (estoque.empty()) { mensagem("Estoque vazio", "Nenhum item cadastrado."); return; }

    vector<string> linhas;
    for (const auto& item : estoque) {
        linhas.push_back(item.nome + " | Qtd: " + to_string(item.quantidade) +
                         " | " + (item.quantidade < config::LIMITE_ESTOQUE_BAIXO ? "CRÍTICO" :
                                   item.quantidade < config::LIMITE_ESTOQUE_BAIXO * 2 ? "BAIXO" : "OK") +
                         " | Nota: " + (item.observacao.empty() ? "-" : item.observacao));
    }

    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({titulo("ESTOQUE ATUAL"),
                      text("CRÍTICO < 10 | BAIXO 10-19 | OK >= 20") | center,
                      separator(), menu->Render() | frame | vscroll_indicator | border | flex,
                      separator(), text("Enter = detalhes | Esc = voltar") | color(Color::GrayDark) | center}) |
               border;
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const auto& item = sistema.getEstoque()[static_cast<size_t>(selecionado)];
            mensagem("Detalhes do item", "Item: " + item.nome +
                     "\nQuantidade: " + to_string(item.quantidade) +
                     "\nStatus: " + (item.quantidade < config::LIMITE_ESTOQUE_BAIXO ? "CRÍTICO" :
                                        item.quantidade < config::LIMITE_ESTOQUE_BAIXO * 2 ? "BAIXO" : "OK") +
                     "\nObservação: " + (item.observacao.empty() ? "Nenhuma" : item.observacao));
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

void editarEstoque(SistemaEstoque& sistema) {
    if (sistema.getEstoque().empty()) { mensagem("Estoque vazio", "Não há itens para editar."); return; }

    vector<string> nomes;
    for (const auto& item : sistema.getEstoque()) nomes.push_back(item.nome);
    int selecionado = 0;
    auto menu = Menu(&nomes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({titulo("EDITAR ESTOQUE"), text("Selecione o item") | center, separator(),
                      menu->Render() | border | flex, separator(),
                      text("Enter = editar | Esc = voltar") | color(Color::GrayDark) | center}) | border;
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const size_t indice = static_cast<size_t>(selecionado);
            const auto item = sistema.getEstoque()[indice];
            tela.Exit();
            const auto r = formulario("EDITAR ITEM", {{"Quantidade", to_string(item.quantidade)}, {"Observação/Nota", item.observacao}});
            if (!r.confirmado) return true;
            int quantidade = 0;
            if (!inteiroValido(r.valores[0], quantidade) || quantidade < 0) {
                mensagem("Erro", "Quantidade inválida.", true);
                return true;
            }
            string erro;
            if (!sistema.editarItem(indice, quantidade, r.valores[1], erro)) mensagem("Erro", erro, true);
            else mensagem("Salvo com sucesso", quantidade < config::LIMITE_ESTOQUE_BAIXO ? "ATENÇÃO: item em nível crítico." : "Item atualizado com sucesso.");
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

// FIX: Remoção com atualização dinâmica da tela sem quebrar a navegação
void removerItem(SistemaEstoque& sistema) {
    while (true) {
        if (sistema.getEstoque().empty()) {
            mensagem("Estoque vazio", "Não há itens para remover.");
            return;
        }

        vector<string> nomes;
        for (const auto& item : sistema.getEstoque()) {
            nomes.push_back(item.nome + " (Qtd: " + to_string(item.quantidade) + ")");
        }

        int selecionado = 0;
        auto menu = Menu(&nomes, &selecionado);
        auto tela = ScreenInteractive::Fullscreen();
        
        bool voltarMenu = false;

        auto renderer = Renderer(menu, [&] {
            return vbox({
                titulo("REMOVER ITEM DO ESTOQUE"),
                text("Selecione o item que deseja excluir definitivamente") | center,
                separator(),
                menu->Render() | border | flex,
                separator(),
                text("Enter = remover | Esc = voltar ao menu") | color(Color::GrayDark) | center
            }) | border;
        });

        renderer = CatchEvent(renderer, [&](Event event) {
            if (event == Event::Escape) {
                voltarMenu = true;
                tela.Exit();
                return true;
            }
            if (event == Event::Return) {
                const size_t idx = static_cast<size_t>(selecionado);
                const string nomeProduto = sistema.getEstoque()[idx].nome;

                if (confirmarAcao("Confirmação de Exclusão", "Você realmente deseja excluir este produto (" + nomeProduto + ")?")) {
                    string nomeRemovido, erro;
                    if (sistema.removerItem(idx, nomeRemovido, erro)) {
                        mensagem("Remoção concluída",
                                 nomeRemovido + " foi removido definitivamente do estoque.");
                    } else {
                        mensagem("Erro ao remover", erro, true);
                    }
                } else {
                    // Se escolher NAO, nada é alterado.
                    mensagem("Remoção cancelada",
                             "O item continua no estoque.");
                }

                tela.Exit();
                return true;
            }
            return false;
        });

        tela.Loop(renderer);

        if (voltarMenu) break;
    }
}

void itensCriticos(SistemaEstoque& sistema) {
    vector<string> linhas;
    for (const auto& item : sistema.getEstoque())
        if (item.quantidade < config::LIMITE_ESTOQUE_BAIXO)
            linhas.push_back(item.nome + " | Quantidade: " + to_string(item.quantidade));

    if (linhas.empty()) { mensagem("Estoque em dia", "Nenhum item está abaixo de 10 unidades."); return; }
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    executarTela("REPOSIÇÃO", menu);
}

void menuEstoque(SistemaEstoque& sistema) {
    const vector<string> opcoes = {"Adicionar Item", "Ver Estoque", "Editar Estoque", "Remover Item", "O que Precisa Comprar", "Voltar"};
    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({titulo("ESTOQUE"),
                      hbox({card("ITENS", to_string(sistema.getEstoque().size()), Color::Cyan),
                            card("CRÍTICOS", to_string(sistema.quantidadeItensBaixos()), Color::Red),
                            card("UNIDADES", to_string(sistema.totalUnidades()), Color::Green)}),
                      separator(), menu->Render() | border | flex, separator(),
                      text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) | border;
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: cadastrarItem(sistema); break;
                case 1: listarEstoque(sistema); break;
                case 2: editarEstoque(sistema); break;
                case 3: removerItem(sistema); break;
                case 4: itensCriticos(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

// ============================================================
// Vendas e devoluções
// ============================================================
void registrarVenda(SistemaEstoque& sistema) {
    if (sistema.getEstoque().empty()) { mensagem("Sem estoque", "Cadastre pelo menos um item antes de registrar uma venda.", true); return; }
    vector<string> nomes;
    for (const auto& item : sistema.getEstoque()) nomes.push_back(item.nome);
    int selecionado = 0;
    auto menu = Menu(&nomes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({titulo("REGISTRAR VENDA"), text("Selecione o item vendido") | center, separator(),
                      menu->Render() | border | flex, separator(), text("Enter = continuar | Esc = voltar") | color(Color::GrayDark) | center}) | border;
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const size_t indice = static_cast<size_t>(selecionado);
            tela.Exit();
            const auto r = formulario("DADOS DA VENDA", {{"Quantidade vendida", "1"}, {"Cliente", ""}});
            if (!r.confirmado) return true;
            int quantidade = 0;
            if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0) { mensagem("Erro", "Quantidade inválida.", true); return true; }
            const int disponivel = sistema.getEstoque()[indice].quantidade;
            if (quantidade > disponivel) {
                mensagem("Quantidade acima do estoque", "A venda será registrada, mas apenas " + to_string(disponivel) + " unidades serão retiradas do estoque.", true);
            }
            string erro;
            if (!sistema.registrarVenda(indice, quantidade, r.valores[1], erro)) mensagem("Erro", erro, true);
            else mensagem("Salvo com sucesso", "Venda salva e estoque atualizado.");
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

void listarVendas(SistemaEstoque& sistema) {
    const auto& vendas = sistema.getVendas();
    if (vendas.empty()) { mensagem("Histórico vazio", "Nenhuma venda registrada."); return; }
    vector<string> linhas;
    for (size_t i = 0; i < vendas.size(); ++i) linhas.push_back(to_string(i + 1) + ". " + vendas[i].item + " | Qtd: " + to_string(vendas[i].quantidade) + " | Cliente: " + vendas[i].cliente);
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] { return vbox({titulo("HISTÓRICO DE VENDAS"), text("Vendas registradas: " + to_string(vendas.size())) | center, separator(), menu->Render() | frame | vscroll_indicator | border | flex, separator(), text("Enter = detalhes | Esc = voltar") | color(Color::GrayDark) | center}) | border; });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) { const auto& v = sistema.getVendas()[static_cast<size_t>(selecionado)]; mensagem("Detalhes da venda", "Item: " + v.item + "\nQuantidade: " + to_string(v.quantidade) + "\nCliente: " + v.cliente); return true; }
        return false;
    });
    tela.Loop(renderer);
}

void editarVenda(SistemaEstoque& sistema) {
    if (sistema.getVendas().empty()) { mensagem("Histórico vazio", "Não há vendas para editar."); return; }
    vector<string> linhas;
    for (const auto& v : sistema.getVendas()) linhas.push_back(v.item + " | Qtd: " + to_string(v.quantidade) + " | Cliente: " + v.cliente);
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] { return vbox({titulo("EDITAR VENDA"), menu->Render() | border | flex, separator(), text("Enter = editar | Esc = voltar") | color(Color::GrayDark) | center}) | border; });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const size_t indice = static_cast<size_t>(selecionado);
            const auto venda = sistema.getVendas()[indice];
            tela.Exit();
            const auto r = formulario("EDITAR VENDA", {{"Quantidade", to_string(venda.quantidade)}, {"Cliente", venda.cliente}});
            if (!r.confirmado) return true;
            int quantidade = 0;
            if (!inteiroValido(r.valores[0], quantidade) || quantidade <= 0) { mensagem("Erro", "Quantidade inválida.", true); return true; }
            string erro;
            if (!sistema.editarVenda(indice, quantidade, r.valores[1], erro)) mensagem("Erro", erro, true);
            else mensagem("Salvo com sucesso", "A edição altera o histórico da venda.");
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

void removerVenda(SistemaEstoque& sistema) {
    while (true) {
        if (sistema.getVendas().empty()) { mensagem("Histórico vazio", "Não há vendas para remover."); return; }
        vector<string> linhas;
        for (const auto& v : sistema.getVendas()) linhas.push_back(v.item + " | Qtd: " + to_string(v.quantidade) + " | Cliente: " + v.cliente);
        int selecionado = 0;
        auto menu = Menu(&linhas, &selecionado);
        auto tela = ScreenInteractive::Fullscreen();
        bool voltarMenu = false;

        auto renderer = Renderer(menu, [&] { return vbox({titulo("REMOVER VENDA"), menu->Render() | border | flex, separator(), text("Enter = remover | Esc = voltar") | color(Color::GrayDark) | center}) | border; });
        renderer = CatchEvent(renderer, [&](Event event) {
            if (event == Event::Escape) { voltarMenu = true; tela.Exit(); return true; }
            if (event == Event::Return) {
                if (confirmarAcao("Confirmação", "Você realmente deseja excluir esta venda?")) {
                    string erro;
                    if (!sistema.removerVenda(static_cast<size_t>(selecionado), erro)) {
                        mensagem("Erro", erro, true);
                    } else {
                        mensagem("Salvo com sucesso", "A venda foi removida do histórico.");
                    }
                }
                tela.Exit();
                return true;
            }
            return false;
        });
        tela.Loop(renderer);
        if (voltarMenu) break;
    }
}

void mostrarRankingItens(SistemaEstoque& sistema) {
    const auto ranking = sistema.rankingItens();
    if (ranking.empty()) { mensagem("Ranking vazio", "Ainda não existem vendas para calcular o ranking."); return; }
    vector<string> linhas;
    for (size_t i = 0; i < ranking.size(); ++i) linhas.push_back(to_string(i + 1) + "º  " + ranking[i].nome + " | Total vendido: " + to_string(ranking[i].totalVendido));
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    executarTela("RANKING DE VENDAS", menu);
}

void tabelaClientes(SistemaEstoque& sistema) {
    const auto ranking = sistema.rankingClientes();
    if (ranking.empty()) { mensagem("Tabela vazia", "Ainda não existem clientes com compras registradas."); return; }
    vector<string> linhas;
    for (size_t i = 0; i < ranking.size(); ++i) linhas.push_back(to_string(i + 1) + "º  " + ranking[i].nome + " | Total comprado: " + to_string(ranking[i].totalComprado));
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    executarTela("CLIENTES", menu);
}

void editarClientes(SistemaEstoque& sistema) {
    const auto ranking = sistema.rankingClientes();
    if (ranking.empty()) { mensagem("Tabela vazia", "Não há clientes para editar."); return; }
    vector<string> nomes;
    for (const auto& c : ranking) nomes.push_back(c.nome);
    int selecionado = 0;
    auto menu = Menu(&nomes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] { return vbox({titulo("EDITAR CLIENTE"), menu->Render() | border | flex, separator(), text("Enter = renomear | Esc = voltar") | color(Color::GrayDark) | center}) | border; });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const string antigo = ranking[static_cast<size_t>(selecionado)].nome;
            tela.Exit();
            const auto r = formulario("RENOMEAR CLIENTE", {{"Novo nome", antigo}});
            if (!r.confirmado) return true;
            string erro;
            if (!sistema.renomearCliente(antigo, r.valores[0], erro)) mensagem("Erro", erro, true);
            else mensagem("Salvo com sucesso", "O nome do cliente foi atualizado.");
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

void registrarDevolucao(SistemaEstoque& sistema) {
    const auto r = formulario("REGISTRAR DEVOLUÇÃO", {{"Item", ""}, {"Quantidade devolvida", "1"}, {"Cliente", ""}});
    if (!r.confirmado) return;
    int quantidade = 0;
    if (!inteiroValido(r.valores[1], quantidade) || quantidade <= 0) { mensagem("Erro", "Quantidade inválida.", true); return; }
    string erro;
    if (!sistema.registrarDevolucao(r.valores[0], quantidade, r.valores[2], erro)) mensagem("Erro", erro, true);
    else mensagem("Salvo com sucesso", "A quantidade devolvida retornou ao estoque.");
}

void editarDevolucao(SistemaEstoque& sistema) {
    if (sistema.getDevolucoes().empty()) { mensagem("Devoluções vazias", "Não há devoluções para editar."); return; }
    vector<string> linhas;
    for (const auto& d : sistema.getDevolucoes()) linhas.push_back(d.item + " | Qtd: " + to_string(d.quantidade) + " | Cliente: " + d.cliente);
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] { return vbox({titulo("EDITAR DEVOLUÇÃO"), menu->Render() | border | flex, separator(), text("Enter = editar | Esc = voltar") | color(Color::GrayDark) | center}) | border; });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const size_t indice = static_cast<size_t>(selecionado);
            const auto antiga = sistema.getDevolucoes()[indice];
            tela.Exit();
            const auto r = formulario("EDITAR DEVOLUÇÃO", {{"Item", antiga.item}, {"Quantidade", to_string(antiga.quantidade)}, {"Cliente", antiga.cliente}});
            if (!r.confirmado) return true;
            int quantidade = 0;
            if (!inteiroValido(r.valores[1], quantidade) || quantidade <= 0) { mensagem("Erro", "Quantidade inválida.", true); return true; }
            string erro;
            if (!sistema.editarDevolucao(indice, r.valores[0], quantidade, r.valores[2], erro)) mensagem("Erro", erro, true);
            else mensagem("Salvo com sucesso", "A devolução e o estoque foram atualizados.");
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

void listarDevolucoes(SistemaEstoque& sistema) {
    if (sistema.getDevolucoes().empty()) { mensagem("Devoluções vazias", "Nenhuma devolução registrada."); return; }
    vector<string> linhas;
    for (const auto& d : sistema.getDevolucoes()) linhas.push_back(d.item + " | Qtd: " + to_string(d.quantidade) + " | Cliente: " + d.cliente);
    int selecionado = 0;
    auto menu = Menu(&linhas, &selecionado);
    executarTela("DEVOLUÇÕES", menu);
}

void menuVendas(SistemaEstoque& sistema) {
    const vector<string> opcoes = {"Registrar Venda", "Ver Histórico de Vendas", "Editar Venda", "Remover Venda", "Ranking de Vendas", "Tabela de Clientes", "Editar Clientes", "Registrar Devolução", "Editar Devolução", "Ver Devoluções", "Voltar"};
    int selecionado = 0;
    auto menu = Menu(&opcoes, &selecionado);
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(menu, [&] {
        return vbox({titulo("VENDAS"),
                      hbox({card("VENDAS", to_string(sistema.getVendas().size()), Color::Green), card("DEVOLUÇÕES", to_string(sistema.getDevolucoes().size()), Color::Yellow)}),
                      separator(), menu->Render() | border | flex, separator(), text("Enter = selecionar | Esc = voltar") | color(Color::GrayDark) | center}) | border;
    });
    renderer = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Escape) { tela.Exit(); return true; }
        if (event == Event::Return) {
            const int acao = selecionado;
            tela.Exit();
            switch (acao) {
                case 0: registrarVenda(sistema); break;
                case 1: listarVendas(sistema); break;
                case 2: editarVenda(sistema); break;
                case 3: removerVenda(sistema); break;
                case 4: mostrarRankingItens(sistema); break;
                case 5: tabelaClientes(sistema); break;
                case 6: editarClientes(sistema); break;
                case 7: registrarDevolucao(sistema); break;
                case 8: editarDevolucao(sistema); break;
                case 9: listarDevolucoes(sistema); break;
                default: break;
            }
            return true;
        }
        return false;
    });
    tela.Loop(renderer);
}

// ============================================================
// Painel principal
// ============================================================
void dashboard(SistemaEstoque& sistema) {
    auto voltar = Button("Voltar", [] {});
    auto tela = ScreenInteractive::Fullscreen();
    auto renderer = Renderer(voltar, [&] {
        return vbox({titulo("PAINEL PRINCIPAL"), text("Gerenciador de estoque") | center | color(Color::GrayLight), separator(),
                      hbox({card("ITENS", to_string(sistema.getEstoque().size()), Color::Cyan),
                            card("UNIDADES", to_string(sistema.totalUnidades()), Color::Green),
                            card("CRÍTICOS", to_string(sistema.quantidadeItensBaixos()), Color::Red),
                            card("VENDAS", to_string(sistema.getVendas().size()), Color::Yellow),
                            card("DEVOLUÇÕES", to_string(sistema.getDevolucoes().size()), Color::Magenta)}),
                      separator(),
                      text(sistema.quantidadeItensBaixos() > 0 ? "Atenção: existem itens abaixo do limite de reposição." : "Estoque sem itens em nível crítico.") |
                          center | (sistema.quantidadeItensBaixos() > 0 ? color(Color::Red) : color(Color::Green)),
                      separator(), voltar->Render() | center}) | border;
    });
    renderer = CatchEvent(renderer, [&](Event event) { if (event == Event::Escape || event == Event::Return) { tela.Exit(); return true; } return false; });
    tela.Loop(renderer);
}

int main() {
    SistemaEstoque sistema;
    if (!sistema.carregarTudo()) {
        mensagem("Aviso", "Alguns arquivos de dados não puderam ser lidos. Os registros válidos foram carregados.", true);
    }

    while (true) {
        const vector<string> opcoes = {"Painel Principal", "Estoque", "Vendas", "Salvar Estoque", "Sair"};
        int selecionado = 0;
        auto menu = Menu(&opcoes, &selecionado);
        auto tela = ScreenInteractive::Fullscreen();
        auto renderer = Renderer(menu, [&] {
            return vbox({text("GERENCIAMENTO DE ESTOQUE E VENDAS") | bold | color(Color::Cyan) | center,
                          separator(),
                          hbox({card("Itens", to_string(sistema.getEstoque().size()), Color::Cyan),
                                card("Críticos", to_string(sistema.quantidadeItensBaixos()), Color::Red),
                                card("Vendas", to_string(sistema.getVendas().size()), Color::Green),
                                card("Devoluções", to_string(sistema.getDevolucoes().size()), Color::Yellow)}),
                          separator(), menu->Render() | border | flex, separator(),
                          text("Setas/Tab = navegar | Enter = abrir | Esc = sair") | color(Color::GrayDark) | center}) |
                   border | bgcolor(Color::RGB(10, 15, 25));
        });
        int acao = -1;
        renderer = CatchEvent(renderer, [&](Event event) {
            if (event == Event::Escape) { acao = 4; tela.Exit(); return true; }
            if (event == Event::Return) { acao = selecionado; tela.Exit(); return true; }
            return false;
        });
        tela.Loop(renderer);

        switch (acao) {
            case 0:
                dashboard(sistema);
                break;

            case 1:
                menuEstoque(sistema);
                break;

            case 2:
                menuVendas(sistema);
                break;

            case 3:
                if (sistema.salvarApenasEstoque()) {
                    mensagem("Salvo com sucesso", "Estoque salvo com sucesso!");
                } else {
                    mensagem("Erro", "Falha ao salvar o estoque.", true);
                }
                break;

            case 4:
                sistema.salvarTudo();
                cout << "\nSistema encerrado pelo usuário.\n";
                return 0;

            default:
                break;
        }
    }

    return 0;
}