#pragma once
#include "save/Profile.h"
#include "shared/GameplayView.h"

enum class NestMenuAction { None, Moved, Buy };

// Navegacao do menu do ninho, em duas etapas. Quem compra e a sessao da partida.
class NestMenu {
public:
    static constexpr int kBackOption = 4;      // Ultima opcao das listas: volta para a raiz.
    static constexpr int kOptionCount = 5;

    void open();
    void close();
    NestMenuAction press(ArmSide side);

    NestMenuPage page() const { return page_; }
    int selected() const { return selected_; }

private:
    NestMenuAction pressOnRoot(ArmSide side);
    NestMenuAction pressOnList(ArmSide side);

    NestMenuPage page_ = NestMenuPage::Closed;
    int selected_ = 0;
};

// Monta o que a interface mostra na pagina atual, com precos e rotulos dos dois bracos.
NestMenuView describeNestMenu(const NestMenu& menu, const Profile& profile);
