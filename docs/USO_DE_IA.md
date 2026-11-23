# Birds&Cashews: uso de inteligência artificial

O enunciado pede que o trabalho indique onde a IA foi usada. Este documento separa dois usos: a IA que faz parte do jogo,
na detecção de pose, e a IA usada como ferramenta para escrever o projeto.

Atualizado em 23/11/2026, com base no histórico do git e nas descrições dos PRs.

## 1. IA dentro do jogo

| Onde | O que é | É IA? |
|---|---|---|
| Detecção de pose (`src/vision/PoseDetector.*`) | Rede neural YOLOv8n-pose, da Ultralytics, executada pelo OpenCV DNN. Recebe a imagem da webcam e devolve 17 pontos do corpo | Sim, modelo treinado pela Ultralytics e usado sem novo treino |
| Reconhecimento de gestos (`src/vision/GestureRecognizer.*`) | Regras sobre os pontos do corpo, medidas pela largura dos ombros, com zona morta e histerese | Não, regras escritas à mão |
| NPCs (`src/ai/`) | Boids para os bandos e máquinas de estados para os outros papéis | Não, algoritmos clássicos de jogos |
| Pássaros e ilha (`src/procedural/`, `src/world/`) | Geração procedural a partir de uma semente | Não |
| Voz (`src/voice/`) | Filtros, redução de ruído espectral e detecção de fala por energia e periodicidade | Não, processamento de sinais |

Os créditos e a licença do modelo estão em [`CREDITOS.md`](CREDITOS.md).

## 2. IA no desenvolvimento

O projeto foi desenvolvido com o **Claude Code**, assistente de programação da Anthropic. Ele foi usado desde o
planejamento: a divisão do trabalho em [`DIVISAO_TRABALHO.md`](DIVISAO_TRABALHO.md) foi pensada para que cada pessoa
disparasse duas tarefas ao mesmo tempo, cada uma com um **subagent** do Claude Code em um git worktree próprio. A seção 9
daquele documento guarda o modelo de prompt usado nas tarefas.

### O que ficou com as pessoas

- Escolha do tema, aprovado pelo professor, e das regras do jogo.
- Ordem das tarefas e o que pedir em cada uma.
- Testes manuais com janela, webcam e som, e os testes de rede entre dois computadores, registrados em
  [`RESULTADOS_TESTE_REDE.md`](RESULTADOS_TESTE_REDE.md).
- Revisão e merge dos PRs.

### Registro por PR

Desde o PR #1, cada PR do Neto tem uma seção "Uso de IA". A tabela resume o que cada uma declara.

| PR | Tarefa | O que o PR declara como gerado com IA |
|---|---|---|
| #1 | T0 Contratos | Contratos, `LocalAuthority` e testes |
| #2 | N2 Perfil e record | Código, testes e descrição, gerados com o Claude Code a partir da especificação da tarefa |
| #3 | N1 Cajus e carga | Código, testes e descrição, gerados com o Claude Code |
| #4 | N3 Ninho e upgrades | Código, testes e descrição, gerados com o Claude Code a partir da especificação da tarefa |
| #7 | N4 Menus, HUD e sons | Código, testes, script das imagens e descrição, a partir das especificações do projeto |
| #8 | Documentação | Texto da atualização de `PLANO.md` e `DIVISAO_TRABALHO.md` |
| #9 | Integração da D4 | Resolução de conflitos, correções de integração e documentação |
| #10 | Redesenho do HUD e dos menus | Redesenho da interface e testes |
| #11 | D5 Voz por proximidade | Código, testes e descrição, verificados com os testes e a análise dos WAVs |
| #12 | D3 e D6 Multiplayer | Código, testes e descrição, gerados com o Claude Code |
| #13 | Multiplayer com voz | Integração, testes e documentação |
| #14 | Ninhos no multiplayer | Código, testes e descrição, revisados pelo Neto |
| #15 | Pássaros e NPCs novos | Código, testes e descrição, escritos com o Claude Code |
| #16 | Junção dos PRs #14 e #15 | Ninhos, comportamentos, gerador de pássaros, integração e testes |
| #17 | Versão do protocolo | Investigação, correção e testes |

### O que não tem registro

- **Primeiro commit** (`7d84bf2`, prova técnica do voo pela webcam): entrou direto na `main`, sem PR, e não tem registro
  de uso de IA.
- **Tarefas D1, D2 e D4 do Daniel** (PRs #5 e #6 e commits de 18 e 19/11/2026): as descrições não têm seção de uso de IA.
  O plano previa o uso do Claude Code também por ele, mas isso não está registrado nesses commits.

### Documentação de entrega

Este documento, o `README.md`, o [`CREDITOS.md`](CREDITOS.md) e a revisão dos documentos de `docs/` para o repositório
público foram redigidos com o Claude Code, a partir do código, do histórico do git e dos PRs.

## 3. Por que os commits não citam a IA

As mensagens de commit não trazem linhas de coautoria nem outra atribuição de IA. O registro do uso fica nas descrições
dos PRs e neste documento.
