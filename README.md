# Birds&Cashews

Jogo em C++17 em que você é um pássaro, controlado pelo próprio corpo diante da webcam. Você voa em primeira pessoa por uma
ilha de cajueiros gigantes, coleta cajus e leva ao seu ninho para melhorar o pássaro ou o ninho. Até 6 jogadores dividem o
mesmo mundo pela rede local e conversam por voz de proximidade.

A pose do jogador vem do modelo YOLOv8n-pose, executado pelo módulo DNN do OpenCV na CPU. O mundo é desenhado com OpenGL,
e a janela, o som e o microfone usam SDL2.

## Situação

O jogo está em desenvolvimento. Voo pelo corpo, cajus, ninho, upgrades, menus, record, NPCs, multiplayer e voz já estão na
`main`, assim como o roubo de ninhos, a colisão em mergulho e os cajus caindo no mundo. Falta validar tudo junto com
dois computadores. A situação de cada tarefa está em [`docs/DIVISAO_TRABALHO.md`](docs/DIVISAO_TRABALHO.md).

## Dependências

Linux com webcam, testado no Ubuntu 22.04 ou mais recente. O jogo também roda sem webcam, pelo teclado.

| Pacote (Ubuntu/Debian) | Para quê |
|---|---|
| `g++`, `make`, `pkg-config` | Compilação |
| `libopencv-dev` | Webcam e detecção de pose (módulo DNN) |
| `libsdl2-dev` | Janela, teclado, som e microfone |
| `libgl-dev` | OpenGL |
| `libopus-dev` | Compressão da voz |
| `libglm-dev` | Matemática 3D |

```bash
sudo apt install g++ make pkg-config libopencv-dev libsdl2-dev libgl-dev libopus-dev libglm-dev
```

## Compilar

```bash
make -j8
```

O executável `birds` fica na raiz do repositório. `make clean` apaga a compilação.

## Rodar

```bash
./birds            # voa com o corpo, pela webcam
./birds --teclado  # voa com o teclado, sem webcam
```

O jogo procura `models/` e `assets/` ao lado do executável. Os perfis e o record ficam em `saves/`, também ao lado do
executável. Se a webcam não abrir, a calibração oferece continuar pelo teclado.

| Opção | O que faz |
|---|---|
| `--teclado` | Joga com o teclado |
| `--camera N` | Usa a webcam de índice `N` |
| `--gestos [camera]` | Abre o laboratório de gestos, que mostra a pose e o gesto reconhecido |
| `--selftest` | Roda os testes automáticos |
| `--benchmark` | Mede o tempo de inferência da pose |
| `--camera-test [segundos] [camera]` | Mede a detecção de pose com a webcam |
| `--render-test PASTA` | Grava imagens de pontos de vista conhecidos em `PASTA`, sem janela visível |
| `--soak [segundos]` | Roda o jogo sem janela visível por um tempo, para testar estabilidade |
| `--rede-teste host [segundos]` | Hospeda uma sala sem janela |
| `--rede-teste entrar IP [semente]` | Entra em uma sala sem janela, coleta e entrega 2 cajus |
| `--voz-teste` | Toca a sua voz como um pássaro virtual que circula ao redor. Use fones. Ctrl+C sai |
| `--voz-teste arquivo SAIDA.wav` | Gera um WAV de demonstração da voz, sem microfone |
| `--help` | Lista as opções |

O `--selftest` grava em pastas temporárias e não altera `saves/`. O `--render-test` cria em `saves/` perfis de exemplo,
como "Jogador" e "Ninho maximo".

## Como jogar

### Ciclo

1. Voe até um cajueiro. Os cajus nascem nas copas e reaparecem com o tempo.
2. Passe perto de um caju para coletar. A carga começa com 3 cajus.
3. Pouse no seu ninho, indicado pela bússola do HUD. O menu do ninho abre sozinho.
4. Cada caju entregue soma 1 ao seu total e vira moeda.
5. Gaste a moeda com **comer** (velocidade, agilidade, carga e alcance da coleta) ou **investir** (defesa, alarme, cofre e
   tamanho do ninho). Depois, bata os braços para decolar.

O record é o maior total de cajus entregues entre os perfis daquele computador. Um aviso aparece quando você o ultrapassa.
As regras completas estão em [`docs/PLANO.md`](docs/PLANO.md).

### Roubo e colisões

- Pouse no ninho de um rival para roubar. Uma barra enche e, a cada volta, um caju vai para a sua carga. Bata os braços
  para fugir. Cajus roubados viram moeda, mas não somam no seu total nem no record.
- Defesa deixa o roubo mais lento, o Cofre guarda parte das moedas e o Alarme mostra na bússola quem está roubando.
- Mergulhe em quem carrega cajus para derrubar toda a carga. Bater com força numa árvore derruba um caju.
- Cajus derrubados caem até o chão e ficam 30 s para quem chegar primeiro.

### Com o corpo

Fique de frente para a webcam, com o corpo inteiro no quadro. A tela de calibração confere isso antes de jogar. A imagem
é espelhada, como um espelho.

| Gesto | Efeito |
|---|---|
| Descer os dois punhos rapidamente | Bate as asas e empurra o pássaro. Também decola do ninho |
| Braços abertos na altura dos ombros | Plana, perdendo altura devagar |
| Braços erguidos acima dos ombros | Sobe, trocando velocidade por altitude |
| Braços abaixados junto ao corpo | Mergulha e acelera |
| Inclinar a linha entre os punhos | Faz a curva, como um avião |
| Erguer o braço esquerdo por 0,7 s | Nos menus, passa para a próxima opção |
| Erguer o braço direito por 0,7 s | Nos menus, confirma |

No menu do ninho, a primeira escolha é entre comer (braço esquerdo) e investir (braço direito). Dentro de uma lista, o
braço esquerdo passa para a próxima opção e o direito compra.

### Com o teclado

| Tecla | Efeito |
|---|---|
| Espaço | Bate as asas e decola do ninho |
| A e D, ou setas esquerda e direita | Curva |
| S ou seta para baixo | Mergulha |
| Q e E | Equivalem a erguer o braço esquerdo e o direito |
| Setas, Enter e Esc | Navegam, confirmam e voltam nos menus |
| Esc | Pausa durante o voo |
| F11 | Alterna a tela cheia |

Sem nenhuma tecla, o pássaro plana.

## Multiplayer na rede local

Um jogador hospeda e os outros entram, até 6 no total. Todos precisam estar na mesma rede e com a mesma versão do jogo.

1. No primeiro computador, escolha **Hospedar sala** no menu principal.
2. Nos outros, escolha **Entrar em sala**. A sala aparece na lista. Se não aparecer, digite o IP do host, que pode ser
   visto com `hostname -I`.

O jogo usa UDP na porta 42917. Se a sala não for encontrada, verifique se a rede permite comunicação entre os
dispositivos e se o firewall libera essa porta. Redes de celular em modo hotspot às vezes bloqueiam a descoberta
automática, e nesse caso o IP digitado resolve.

A voz por proximidade funciona só dentro de uma sala. Ela é transmitida quando o jogador fala, com filtro de voz de
pássaro, volume pela distância e posição estéreo. Use fones para evitar eco.

Para testar na mesma máquina, abra `./birds` e hospede. Em outro terminal, abra `./birds --teclado` e entre pelo IP
`127.0.0.1`. O roteiro completo de teste está em [`docs/TESTE_REDE.md`](docs/TESTE_REDE.md).

## Testes automáticos

```bash
./birds --selftest   # ou make test
```

Os testes cobrem gestos, voo, cajus, ninho, perfis e record, interface, NPCs, pássaros procedurais, rede e voz. Eles não
abrem janela nem usam a webcam. Em 23/11/2026, a `main` passava em todas as 911 verificações. O programa termina com
código 0 quando tudo passa.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/vision/` | Webcam, detecção de pose e reconhecimento de gestos |
| `src/input/` | Entrada pelo corpo e pelo teclado |
| `src/core/` | Laço principal do jogo e modos de teste |
| `src/world/` | Ilha gerada por semente e cajueiros |
| `src/entity/` | Pássaro do jogador e física de voo |
| `src/game/` | Cajus, ninho, upgrades e regras da partida |
| `src/save/` | Perfis, mundo salvo e record |
| `src/ui/` | Menus, calibração, HUD e sons |
| `src/procedural/` | Geração dos pássaros por semente |
| `src/ai/` | Ecossistema de NPCs |
| `src/net/` | Rede local, protocolo e replicação |
| `src/voice/` | Voz por proximidade |
| `src/gfx/` | Janela, OpenGL e desenho do mundo |
| `src/shared/` | Contratos entre os módulos |
| `assets/` | Imagens PNG, fonte e créditos |
| `models/` | Modelo de pose em ONNX |
| `tests/` | Programa de teste da rede em dois computadores |
| `tools/` | Script que gera as imagens de `assets/` |
| `docs/` | Plano, divisão do trabalho, testes de rede, créditos e uso de IA |

## Documentação

| Documento | Conteúdo |
|---|---|
| [`docs/PLANO.md`](docs/PLANO.md) | Regras do jogo, mecânicas, arquitetura e requisitos do enunciado |
| [`docs/DIVISAO_TRABALHO.md`](docs/DIVISAO_TRABALHO.md) | Tarefas, responsáveis e situação de cada uma |
| [`docs/TESTE_REDE.md`](docs/TESTE_REDE.md) | Roteiro para testar a rede em um ou dois computadores |
| [`docs/RESULTADOS_TESTE_REDE.md`](docs/RESULTADOS_TESTE_REDE.md) | Resultados dos testes de rede já feitos |
| [`docs/CREDITOS.md`](docs/CREDITOS.md) | Créditos e licenças de terceiros |
| [`docs/USO_DE_IA.md`](docs/USO_DE_IA.md) | Onde a IA foi usada no desenvolvimento e no jogo |
| Diagrama UML de classes | Em produção, depois da arquitetura final |

## Créditos e licenças

- **Fonte:** Poppins Bold, da The Poppins Project Authors, sob a SIL Open Font License 1.1.
- **Modelo de pose:** YOLOv8n-pose, da Ultralytics, sob a licença AGPL-3.0.
- **Imagens e sons:** originais do projeto. As imagens são geradas por `tools/gerar_assets.py`, e os sons são sintetizados
  pelo código.

Os detalhes estão em [`docs/CREDITOS.md`](docs/CREDITOS.md). Este repositório ainda não define uma licença para o código
do jogo.

## Autores

Neto ([@fermneto](https://github.com/fermneto)) e Daniel ([@dnieldantas](https://github.com/dnieldantas)). Daniel pausou
a participação no projeto, e as tarefas dele passaram para o Neto.
