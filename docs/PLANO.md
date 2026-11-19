# Birds&Cashews: plano de desenvolvimento

Projeto de Linguagem de Programação I, semestre 2026.2, prof. Derzu Omaia. **Entrega: 07/12/2026.**

## 1. Resumo do jogo

Você é um pássaro em primeira pessoa, controlado pelo próprio corpo diante da webcam, em uma ilha de cajueiros gigantes.
Você coleta cajus, leva ao seu ninho e decide entre comer (melhora o pássaro) ou investir (melhora o ninho). Até 4
jogadores dividem o mundo pela rede local, conversam por voz de proximidade e podem roubar cajus uns dos outros.

## 2. Decisões fechadas

| Tema | Decisão |
|---|---|
| Plataforma | App desktop Linux em C++17, com OpenGL, SDL2 e OpenCV DNN |
| Rede | Um jogador hospeda, os outros encontram a sala na LAN. De 1 a 4 jogadores |
| Estrutura | Mundo aberto contínuo, sem fim de partida |
| Interação | Roubo de cajus de ninhos rivais e colisão em mergulho derruba a carga |
| Coleta | Automática por proximidade, com o bico abrindo sozinho |
| Bico | Também abre enquanto o jogador fala |
| Ninho | Menu por postura, segurando para confirmar |
| NPCs | Ecossistema: pequenos em bando, médios competem por cajus, grandes caçam |
| Salvamento | Pássaro e ninho no perfil local, estado do mundo no host |
| Record | Total acumulado de cajus entregues |
| Voz | Microfone aberto por detecção de fala, com filtro de voz de pássaro |
| Esforço | Bater os braços livremente, sem barra de energia |
| Mapa | Ilha low-poly gerada por semente, com o mar como limite |

## 3. Mecânicas

### 3.1 Controle pelo corpo

Todas as medidas são divididas pela largura dos ombros, para não depender da distância até a câmera.

| Gesto | Detecção | Efeito |
|---|---|---|
| Bater | Descida rápida dos dois punhos | Empurra o pássaro enquanto as asas descem. Também decola do ninho |
| Planar | Braços abertos na altura dos ombros | Perde altura devagar e mantém a velocidade |
| Subir | Braços erguidos acima dos ombros | Troca velocidade por altitude, de forma proporcional à altura dos braços |
| Mergulhar | Braços abaixados junto ao corpo | Desce e acelera, de forma proporcional à altura dos braços |
| Curvar | Ângulo entre os punhos, com zona morta de 8° e limite de 45° | Inclina e faz a curva como um avião |
| Menu do ninho | Braço esquerdo ou direito erguido por 0,7 s | Escolhe a opção |

As asas vistas em primeira pessoa acompanham a altura de cada braço. Em alta velocidade, rajadas de vento aparecem na tela.

Se o corpo sair do quadro, o pássaro plana nivelado e a tela avisa. A webcam aparece espelhada em uma janela pequena,
com o esqueleto desenhado por cima. Antes de jogar, uma tela de calibração confere se o corpo inteiro está visível.

### 3.2 Ciclo de jogo

1. Voar até um cajueiro. Os cajus nascem nas copas e reaparecem com o tempo.
2. Passar perto de um caju para coletar, até o limite da carga. Começa com 3.
3. Pousar no ninho, que abre o menu automaticamente.
4. Cada caju entregue soma 1 ao total e vira moeda guardada no ninho.
5. Gastar a moeda com **comer** ou **investir**, e decolar batendo os braços.

| Comer: pássaro | Investir: ninho |
|---|---|
| Velocidade | Defesa: aumenta o tempo necessário para roubar |
| Agilidade nas curvas | Alarme: avisa e mostra o ladrão |
| Capacidade de carga | Cofre: parte dos cajus não pode ser roubada |
| Alcance da coleta | Tamanho: ninho maior e bônus de moedas na entrega |

No menu do ninho, a primeira escolha é entre comer (braço esquerdo) e investir (braço direito). Dentro de uma lista, o braço
esquerdo passa para a próxima opção e o direito compra. Bater os braços decola.

| Upgrade | Nível 1 | Nível 2 | Nível 3 | Nível 4 | Nível 5 | Efeito por nível |
|---|---|---|---|---|---|---|
| Velocidade | 3 | 6 | 10 | 15 | 22 | +8% de velocidade |
| Agilidade | 2 | 5 | 9 | 14 | 20 | +12% de agilidade |
| Capacidade | 4 | 8 | 13 | 19 | 26 | +1 caju de carga |
| Alcance | 2 | 4 | 8 | 13 | 18 | +2,5 m de coleta |
| Defesa | 3 | 6 | 10 | 15 | 22 | Sem efeito até a N5 |
| Alarme | 3 | 6 | 10 | 15 | 22 | Sem efeito até a N5 |
| Cofre | 4 | 8 | 13 | 19 | 26 | Sem efeito até a N5 |
| Tamanho | 3 | 7 | 12 | 18 | 25 | Ninho 18% maior e +20% de moedas na entrega |

### 3.3 Roubo e colisões

- Ficar sobre um ninho rival por alguns segundos rouba um caju.
- Colidir em mergulho com quem carrega cajus derruba a carga, e os cajus caem no mundo.
- Bater em árvore reduz a velocidade e derruba um caju. Não existe morte.
- **Cajus roubados valem moeda, mas não somam no record.** Isso impede dois jogadores de inflarem o total trocando cajus.
- Um ninho só existe no mundo enquanto o dono está conectado, então ninguém é roubado offline.

### 3.4 Pontuação e record

- **Pontuação atual:** total de cajus entregues pelo perfil.
- **Record:** maior total já registrado entre os perfis vistos naquele computador, salvo em `record.txt`.
- **Aviso na tela:** aparece quando o seu total ultrapassa o record. Enquanto você lidera, o HUD mostra "RECORD".

Essa regra mantém sua escolha de total acumulado e dá sentido ao aviso exigido pelo enunciado, porque comparar o total
só com ele mesmo faria o record ser batido a cada caju.

### 3.5 Pássaros procedurais

Cada pássaro nasce de uma semente de 32 bits, que alimenta um genoma determinístico:

1. **Corpo:** gera seções elípticas ao longo de uma curva, definindo tamanho e proporções.
2. **Partes:** sorteia bico, crista, cauda, asas e pernas em famílias paramétricas de polígonos.
3. **Encaixe:** cada parte se liga a um ponto de encaixe do corpo e escala conforme o tamanho dele.
4. **Cor:** gera uma paleta harmônica em HSV.
5. **Comportamento:** massa e envergadura definem o papel no ecossistema.

| Porte | Comportamento |
|---|---|
| Pequeno (6 pássaros) | Voa em bando usando o algoritmo Boids |
| Médio (2 pássaros) | Procura e disputa cajus |
| Grande (1 pássaro) | Persegue quem carrega 2 cajus ou mais e derruba a carga |

Quem está pousado no ninho não é atacado. Por enquanto, a carga derrubada some do mundo. Os cajus caindo chegam com a N5.

Pela rede trafega só a semente, e cada computador reconstrói o mesmo pássaro.

### 3.6 Voz por proximidade

O áudio é capturado em 48 kHz, passa por detecção de fala, recebe o filtro de voz de pássaro e é comprimido com Opus. O
host repassa cada pacote apenas aos jogadores dentro do alcance. Quem recebe aplica volume por distância e posição
estéreo. O indicador de fala abre o bico do pássaro. Fones são recomendados para evitar eco.

## 4. Arquitetura

```
Thread da câmera  -> PoseDetector -> GestureRecognizer -> FlightIntent
Thread principal  -> Game -> World, Renderer, HUD
Thread de rede    -> NetSession (UDP) <-> Snapshots e eventos
Thread de áudio   -> VoiceChat (SDL2 + Opus)
```

| Módulo | Classes principais |
|---|---|
| `vision/` | `Camera`, `PoseDetector`, `GestureRecognizer`, `VisionPipeline` |
| `input/` | `FlightInput`, `BodyInput`, `KeyboardInput`, `GestureControls` |
| `core/` | `Game` |
| `world/` | `World`, `Island`, `CashewTree` |
| `entity/` | `PlayerBird`, `FlightModel` |
| `game/` | `GameplaySession`, `CashewField`, `CashewCollector`, `NestMenu` |
| `save/` | `Profile`, `ProfileStore`, `WorldStore`, `RecordManager` |
| `ui/` | `GameUi`, `ScreenFlow`, `CalibrationTracker`, `SoundManager` |
| `procedural/` | `buildBirdMesh` e `buildBirdFlightParts` |
| `ai/` | `NpcEcosystem` |
| `net/` | `UdpSession` (implementa `NetSession`) |
| `gfx/` | `Renderer`, `Shader`, `Mesh`, `Camera3D`, `BirdViewModel` |
| `shared/` | Contratos entre as frentes e `LocalAuthority` |
| `voice/` | Planejado para a D5 |

**Rede:** o host decide cajus, NPCs, roubos e colisões. Cada cliente simula o próprio voo para responder sem atraso. O
host envia o estado 20 vezes por segundo e os clientes interpolam os outros pássaros.

## 5. Requisitos do enunciado

| # | Requisito | Como será atendido | Situação |
|---|---|---|---|
| 1 | Diagrama UML de classes | `docs/uml_classes.png`, gerado a partir da arquitetura final | Pendente (F2) |
| 2 | Bastante métodos | Classes pequenas por módulo | Atendido |
| 3 | Rodar até sair, com menu | Menu principal: hospedar, entrar, calibrar, perfil, sair | Atendido |
| 4 | Pontuação, record e arquivo | Seção 3.4, com `record.txt` | Atendido |
| 5 | Imagens com fundo transparente | Caju, ícones de upgrades e HUD em PNG com alfa | Atendido |
| 6 | OpenCV, YOLO ou OpenPose | YOLOv8n-pose executado pelo OpenCV DNN | Atendido |
| 7 | Jogabilidade | Calibração, zonas mortas e suavização dos gestos | Em validação |
| 8 | Vídeo espelhado | Imagem e gestos tratados em modo espelho | Atendido |
| 9 | Detecção em todo o vídeo | Teste de cobertura nas bordas durante a calibração | Falta validar com a webcam |
| 10 | Som em eventos | Coleta, entrega, roubo, upgrade, record e ambiente | Falta validar o áudio |
| Instrução 4 | Indicar uso de IA | `docs/USO_DE_IA.md` | Pendente (F2) |

## 6. Cronograma

As tarefas, os responsáveis e as dependências estão em [`DIVISAO_TRABALHO.md`](DIVISAO_TRABALHO.md).

| Marco | Situação em 18/11/2026 | O que falta |
|---|---|---|
| **M0** Prova técnica | Concluído | Nada |
| **M1** Voo | Concluído | Nada |
| **M2** Ciclo de jogo | Concluído na `main` | Validar no jogo real |
| **M3** Multiplayer | Conexão UDP na `main` | Replicação de jogadores (D3), roubo (N5) e testes no hotspot |
| **M4** Pássaros | Gerador e ecossistema na `main` | Sincronizar os NPCs em rede (D6) |
| **M5** Voz | Não iniciado | Voz por proximidade (D5) |
| **M6** Fechamento | Não iniciado | Teste integrado, README, UML, uso de IA, créditos e apresentação |

Se o prazo apertar, os cortes seguem esta ordem: filtro de voz, caçadores, alarme e cofre do ninho, sons de ambiente.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Pose lenta na CPU, sem GPU dedicada | Medir no M0, reduzir a entrada do modelo e rodar em thread própria |
| Hotspot do celular bloqueia a descoberta automática de salas | Testar antes no mesmo hotspot e manter um campo para digitar o IP do host |
| Gestos confundidos entre si | Máquina de estados com histerese e ajuste com pessoas diferentes |
| Eco na voz | Fones e detecção de fala |
| Escopo grande para 19 dias | Marcos independentes, com jogo funcional ao fim de cada um |

## 8. Pendências

O tema "Outro" foi aprovado pelo professor. A apresentação usará computadores próprios, conectados ao hotspot de um
celular.

1. Validar o jogo integrado com janela visível, webcam e som.
2. Concluir o teste de rede entre dois computadores, incluindo o hotspot. Ver
   [`RESULTADOS_TESTE_REDE.md`](RESULTADOS_TESTE_REDE.md).
3. Implementar D3, D5, D6 e N5.
4. Produzir README, diagrama UML, registro de uso de IA e créditos, incluindo a licença do YOLOv8n-pose.
5. Fazer o teste integrado e ensaiar a apresentação.
