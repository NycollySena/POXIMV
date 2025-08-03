#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

// Índices específicos de cada CSR
// Mapeia endereço CSR para índice no vetor registradoresCSRs[7]
int csrIndex(uint16_t endereco)
{
	switch (endereco)
	{
	case 0x300:
		return 0; // mstatus
	case 0x304:
		return 1; // mie
	case 0x305:
		return 2; // mtvec
	case 0x341:
		return 3; // mepc
	case 0x342:
		return 4; // mcause
	case 0x343:
		return 5; // mtval
	case 0x344:
		return 6; // mip
	default:
		return -1; // CSR não suportado
	}
}

// função para preparar mstatus para o modo de exceção
void prepMstatus(uint32_t *mstatus_ptr)
{
	uint32_t mstatus = *mstatus_ptr;

	// Extrai o bit MIE (bit 3)
	uint32_t mie = (mstatus >> 3) & 0x1;

	// Limpa o bit MPIE (bit 7)
	mstatus &= ~(1u << 7);

	// Copia MIE para MPIE (bit 7)
	mstatus |= (mie << 7);

	// Limpa o bit MIE (bit 3)
	mstatus &= ~(1u << 3);

	// Limpa os bits 11-12 (MPP)
	mstatus &= ~(3u << 11);

	// Define MPP = 0b11 (modo máquina)
	mstatus |= (3u << 11);

	// Atualiza o valor original
	*mstatus_ptr = mstatus;
}

// função para tratar as excessões
void registrarExcecao(uint32_t causa, uint32_t endereco_instrucao, uint32_t tval, uint32_t *registradoresCSRs, FILE *output, uint32_t *pc_ptr)
{
	int indice_mcause = csrIndex(834); // endereço de mcause
	int indice_mepc = csrIndex(833);   // endereço de mepc
	int indice_mtvec = csrIndex(773);  // endereço de mtvec

	if (indice_mcause != -1)
	{
		registradoresCSRs[indice_mcause] = causa; // escrevendo a causa da excessão
	}

	if (indice_mepc != -1)
	{
		registradoresCSRs[indice_mepc] = endereco_instrucao; // escrevendo o endereço da instrução que causou
	}

	// Define novo PC com base em mtvec
	if (indice_mtvec != -1 && pc_ptr != NULL)
	{
		uint32_t mtvec = registradoresCSRs[indice_mtvec];
		uint32_t modo = mtvec & 0x3;  // bits 1:0 definem o modo
		uint32_t base = mtvec & ~0x3; // zera os 2 últimos bits para obter o endereço base

		if (modo == 0)
		{
			// Modo direto
			*pc_ptr = base;
		}
		else if (modo == 1)
		{
			// Modo vetorizado
			*pc_ptr = base + 4 * causa;
		}
		else
		{
			// Modo reservado ou inválido — assume modo direto como fallback
			*pc_ptr = base;
		}
	}

	// Ignora exceção de breakpoint (cause == 3), pois já foi tratada por 'ebreak'
	if (causa == 0x3)
	{
		return; // Evita print duplicado para ebreak
	}

	// Mapeamento de códigos de causa para nomes de exceção (exceto breakpoint)
	const char *nome_exc =
		(causa == 0x0) ? "instruction_misaligned" : (causa == 0x1) ? "instruction_fault"
												: (causa == 0x2)   ? "illegal_instruction"
												: (causa == 0x4)   ? "load_misaligned"
												: (causa == 0x5)   ? "load_fault"
												: (causa == 0x6)   ? "store_misaligned"
												: (causa == 0x7)   ? "store_fault"
												: (causa == 0xB)   ? "environment_call"
																   : "unknown";

	fprintf(output, ">exception:%-20s cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
			nome_exc, causa, endereco_instrucao, tval);
}

int main(int argc, char *argv[])
{ // argumento para abrir o projeto no terminal, entrega a entrada e fala a saida
  // "./meuprograma" "entrada.hex"  "saida.out"

	FILE *input = fopen(argv[1], "r");	// abre um arquivo de entrada
	FILE *output = fopen(argv[2], "w"); // abre/cria em arquivo de saida (os arquivos do argumento do main)

	FILE *input2 = fopen("qemu.terminal.in", "r");	 // Abre o arquivo de entrada UART
	FILE *output2 = fopen("qemu.terminal.out", "w"); // Abre/cria o arquivo de saída UART

	//FILE *input = fopen("input.hex", "r");
	//FILE *output = fopen("output.out", "w");

	// offset é o ponto de partida da memória simulada
	// const uint32_t offset = 0x80000000; // Vamos fingir que a memória do processador começa no endereço 0x80000000. offset significa deslocamento
	const uint32_t offset = 0x80000000;

	uint32_t registradores[32] = {0}; // 32 registradores inicializados com 0
	// abreviações do RISC-V para os registradores0
	const char *regNomes[32] = {"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

	// registradores CSRs
	uint32_t registradoresCSRs[7] = {0};
	// nome dos registradores CSRs
	const char *regNomesCSRs[7] = {"mstatus", "mie", "mtvec", "mepc", "mcause", "mtval", "mip"};

	// registradores uart
	uint32_t registradoresUART[6] = {0};

	// Registradores clint
	uint32_t clint_msip = 0;	  // interrupção de software (MSIP)
	uint64_t clint_mtime = 0;	  // contador de tempo (MTIME)
	uint64_t clint_mtimecmp = -1; // alvo da interrupção (MTIMECMP)

	// Registradores PLIC
	 uint32_t plic_priority = 0;
	 uint32_t plic_pending = 0;
	 uint32_t plic_enable = 0;
	 uint32_t plic_threshold = 0;
	 uint32_t plic_claim = 0;

	// inicialização de mtvec pra ebreak
	// tirar no projeto final
	registradoresCSRs[2] = 0x80000094;

	// registrador pc inicializado com offset. pq o offset é quem esta com o endereço inicial da memoria simulada
	//  o pc é o marca pagina (mostra onde vc esta agora e avança para a proxima instrução)
	//  aponta para o endereço da próxima instrução a ser executada.
	uint32_t pc = offset;

	// 32 KIB alocados dinamicamente para armazenar dados e instruções
	// mem será a memória simulada que o processador acessa durante a execução.
	uint8_t *mem = (uint8_t *)malloc(32 * 1024); // Cada posição de memória armazena 1 byte (8 bits) por isso uint8_t; 1 KiB = 1024 bytes

	// leitura do conteúdo da memória a partir de um arquivo hexadecimal de entrada
	// o input é o ponteiro da entrada
	uint32_t contadorMem = offset; // variavel para atualizar o endereço apartir do offset e depois armazenar na memmoria
	char entrada[1000];			   // variavel para armazenar os carateres do arquivo de entrada

	while (1)
	{
		char enderecoHex[10]; // para armazenar e depois converter o endereço dos dados
		if (fgets(entrada, sizeof(entrada), input) == NULL)
			break; // caso a leitura do arquivo acabe o laço para de rodar
		if (entrada[0] == '@')
		{ // separando o endereço
			for (int i = 0; i < 8; i++)
			{
				enderecoHex[i] = entrada[i + 1]; // ignora o @ e armazenar os 8 caracteres em enderecoHex
			}
			enderecoHex[8] = '\0';						  // para terminar a string
			contadorMem = strtoll(enderecoHex, NULL, 16); // convertendo a entrada de endereço (dado que começa com @)
		}

		else
		{
			unsigned int byte;
			char *p = entrada;
			while (sscanf(p, "%2x", &byte) == 1)
			{
				mem[contadorMem - offset] = (uint8_t)byte;
				contadorMem++;

				while (*p == ' ')
					p++; // pula espaços extras
				p += 2;	 // avança pro próximo par de caracteres hex
			}
		}
	}

	// inicio do simulador de instruções
	uint8_t run = 1; // pra controlar o loop
	// laço principal de execução do simulador
	while (run)
	{ // o loop que vai buscar e decodificar as instruções

		// Tratamento da exceção 1 — Instruction Access Fault. Quando pc está fora da memória válida
		if (pc < offset || pc >= offset + 32 * 1024)
		{
			prepMstatus(&registradoresCSRs[0]);							 // preparar mstatus para a excessão
			registrarExcecao(1, pc, pc, registradoresCSRs, output, &pc); // Instruction access fault
			continue;
		}

		// converte mem para um tipo de 4 bytes, acessa a posição correta da instrução dividindo por 4 para acessar apenas 1  instrução inteira  por indice
		// uint32_t instrucao = ((uint32_t*)mem)[(pc - offset)>>2];
		uint32_t instrucao = ((uint32_t *)(mem))[(pc - offset) >> 2];

		// if (pc < offset || pc >= offset + 32 * 1024)
		//{
		//	printf("PC fora do intervalo da memória: 0x%08x\n", pc);
		//	run = 0;
		//	break; // ou run = 0;
		// }

		// agora cada indice de mem pega 4 bytes que é uma instrução inteira
		// caso não funcione trocara /4 por >>2
		// obtenfdo o opcode para manter os 7 bits menos significativos (da direita) da instrução, e ignorar o resto
		const uint32_t opcode = instrucao & 0b1111111; // o & vai filtrar o bits que queremos pois ele só repete 1 se for exatamente igual então armazena exatamente o mesmo numero no mesmo lugar
		// inicializando os campos que tem que usar para identificar as instruções
		// o >> desloca a quantidade de bits necessaria
		const uint8_t funct7 = instrucao >> 25;			  // complemento, usado em operações
		const int32_t imm_i = ((int32_t)instrucao) >> 20; // valor imediato com sinal (Tipo I)
		// const uint8_t uimm = (instrucao >> 20) & 0b11111; // valor imediato sem sinal (shift, Tipo I)
		const uint8_t rs1 = (instrucao >> 15) & 0b11111;  // registrador de origem 1
		const uint8_t rs2 = (instrucao >> 20) & 0b11111;  // registrador de origem 2
		const uint8_t funct3 = (instrucao >> 12) & 0b111; // diferencia instruções com o mesmo opcode
		const uint8_t rd = (instrucao >> 7) & 0b11111;	  // registrador onde armazena o resultado
														  // const int32_t imm_s = ((instrucao >> 25) << 5) | ((instrucao >> 7) & 0b11111); // imediato do tipo S (armazenamento)
		int32_t imm_b = ((instrucao >> 31) & 0x1) << 12 |
						((instrucao >> 7) & 0x1) << 11 |
						((instrucao >> 25) & 0x3F) << 5 |
						((instrucao >> 8) & 0xF) << 1;

		if (imm_b & 0x1000)
			imm_b |= 0xFFFFE000;

		int32_t imm_j = 0;
		imm_j |= ((instrucao >> 31) & 0x1) << 20;
		imm_j |= ((instrucao >> 21) & 0x3FF) << 1;
		imm_j |= ((instrucao >> 20) & 0x1) << 11;
		imm_j |= ((instrucao >> 12) & 0xFF) << 12;
		if (imm_j & (1 << 20))
			imm_j |= 0xFFF00000; // imediato do tipo J (jump)

		const int32_t imm_u = instrucao & 0xFFFFF000;	   // imediato do tipo U (lui/auipc)
		const uint8_t shamt = (instrucao >> 20) & 0b11111; // extrai 5 bits

		int32_t imm_s = (((instrucao >> 25) & 0x7F) << 5) | ((instrucao >> 7) & 0x1F);
		if (imm_s & 0x800)
			imm_s |= 0xFFFFF000;

		// instruções tipo R
		switch (opcode)
		{
			// tipo R-type
		case 0b0110011:
			// add (soma)
			if (funct3 == 0b000 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] + registradores[rs2];
				fprintf(output, "0x%08x:add %s,%s,%s %s=0x%08x+0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				// Atualizando o registrador de destino, se não for registradores[0]
				if (rd != 0)
				{ // Ela atualiza o registrador destino rd com o valor do cálculo (data), mas só se o registrador rd não for o registrador x0.
					registradores[rd] = resultado;
				}
			}
			// sub (subtração)
			else if (funct7 == 0b0100000 && funct3 == 0b000)
			{
				const uint32_t resultado = registradores[rs1] - registradores[rs2];
				fprintf(output, "0x%08x:sub %s,%s,%s %s=0x%08x-0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// sll (desloca o conteúdo de rs1 logicamente para a esquerda)
			else if (funct3 == 0b001 && funct7 == 0b0000000)
			{
				const uint8_t deslocar = registradores[rs2] & 0b11111;	   // filtra os 5 bits menos significativos
				const uint32_t resultado = registradores[rs1] << deslocar; // desloca os 5 bits a esquerda

				fprintf(output, "0x%08x:sll %s,%s,%s %s=0x%08x<<u5=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// slt (Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2 na comparação com sinal)
			else if (funct3 == 0b010 && funct7 == 0b0000000)
			{
				int32_t sinal_rs1 = (int32_t)registradores[rs1];
				int32_t sinal_rs2 = (int32_t)registradores[rs2];
				const uint32_t resultado = (sinal_rs1 < sinal_rs2) ? 1 : 0; // Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2

				fprintf(output, "0x%08x:slt %s,%s,%s %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// sltu (Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2 na comparação sem sinal)
			else if (funct3 == 0b011 && funct7 == 0b0000000)
			{
				const uint32_t resultado = (registradores[rs1] < registradores[rs2]) ? 1 : 0; // Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2

				fprintf(output, "0x%08x:sltu %s,%s,%s %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// xor( operação bit a bit (lógica) de OU EXCLUSIVO entre os valores dos registradores rs1 e rs2)
			else if (funct3 == 0b100 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] ^ registradores[rs2];

				fprintf(output, "0x%08x:xor %s,%s,%s %s=0x%08x^0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}
			// srl(Desloca os bits do valor em rs1 para a direita, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b101 && funct7 == 0b0000000)
			{
				const uint8_t deslocar = registradores[rs2] & 0b11111;	   // filtra os 5 bits menos significativos
				const uint32_t resultado = registradores[rs1] >> deslocar; // desloca os 5 bits a direita

				fprintf(output, "0x%08x:srl %s,%s,%s %s=0x%08x>>u5=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// sra(Desloca os bits de rs1 para a direita, mantendo o bit de sinal (preenche com 0 se positivo, 1 se negativo)
			else if (funct3 == 0b101 && funct7 == 0b0100000)
			{
				const uint8_t deslocar = registradores[rs2] & 0b11111; // filtra os 5 bits menos significativos
				const int32_t Sinal_rs1 = (int32_t)registradores[rs1];
				const uint32_t resultado = (uint32_t)(Sinal_rs1 >> deslocar);

				fprintf(output, "0x%08x:sra %s,%s,%s %s=0x%08x>>>u5=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// or(Ela realiza um OU bit a bit (bitwise OR) entre os valores contidos nos registradores rs1 e rs2)
			else if (funct3 == 0b110 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] | registradores[rs2];

				fprintf(output, "0x%08x:or %s,%s,%s %s=0x%08x|0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// and (compara os bits de dois registradores (rs1 e rs2) e retorna 1 somente se ambos os bits forem 1, caso contrário, retorna 0)
			else if (funct3 == 0b111 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] & registradores[rs2];

				fprintf(output, "0x%08x:and %s,%s,%s %s=0x%08x&0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// mul(executa uma multiplicação entre os valores inteiros contidos nos registradores rs1 e rs2)
			else if (funct3 == 0b000 && funct7 == 0b0000001)
			{
				const uint32_t resultado = registradores[rs1] * registradores[rs2];

				fprintf(output, "0x%08x:mul %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// mulh(guarda os 32 bits mais significativos da multiplicação com sinal)
			else if (funct3 == 0b001 && funct7 == 0b0000001)
			{
				int64_t rs1_64 = (int64_t)(int32_t)registradores[rs1];
				int64_t rs2_64 = (int64_t)(int32_t)registradores[rs2];
				int64_t produto = rs1_64 * rs2_64;
				const uint32_t resultado = (uint32_t)(produto >> 32); // sem sinal

				fprintf(output, "0x%08x:mulh %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// mulhsu (guarda os 32 bits mais significativos da multiplicação sem sinal)
			else if (funct3 == 0b010 && funct7 == 0b0000001)
			{
				int64_t rs1_64 = (int64_t)(int32_t)registradores[rs1]; // rs1 com sinal
				uint64_t rs2_64 = (uint32_t)registradores[rs2];		   // rs2 sem sinal
				int64_t produto = rs1_64 * rs2_64;					   // resultado 64 bits
				const uint32_t resultado = (uint32_t)(produto >> 32);  // parte alta

				fprintf(output, "0x%08x:mulhsu %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,					// endereço da instrução
						regNomes[rd],		// nome do registrador de destino
						regNomes[rs1],		// nome do registrador rs1
						regNomes[rs2],		// nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// mulhu (Multiplica os valores não assinados de rs1 e rs2 (32 bits cada))
			else if (funct3 == 0b011 && funct7 == 0b0000001)
			{
				uint64_t rs1_64 = (uint32_t)registradores[rs1];
				uint64_t rs2_64 = (uint32_t)registradores[rs2];
				uint64_t produto = rs1_64 * rs2_64;
				const uint32_t resultado = (uint32_t)(produto >> 32);

				fprintf(output, "0x%08x:mulhu %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,					// endereço da instrução
						regNomes[rd],		// nome do registrador de destino
						regNomes[rs1],		// nome do registrador rs1
						regNomes[rs2],		// nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// div( faz a divisão com sinal)
			else if (funct3 == 0b100 && funct7 == 0b0000001)
			{
				int32_t rs1_32 = (int32_t)registradores[rs1];
				int32_t rs2_32 = (int32_t)registradores[rs2];

				const uint32_t resultado = (rs2_32 == 0) ? 0xFFFFFFFF : (rs1_32 == INT32_MIN && rs2_32 == -1) ? (uint32_t)INT32_MIN
																											  : (uint32_t)(rs1_32 / rs2_32);

				fprintf(output, "0x%08x:div %s,%s,%s %s=0x%08x/0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// divu( faz a divisão sem sinal)
			else if (funct3 == 0b101 && funct7 == 0b0000001)
			{
				const uint32_t resultado = (registradores[rs2] == 0) ? 0xFFFFFFFF : registradores[rs1] / registradores[rs2];

				fprintf(output, "0x%08x:divu %s,%s,%s %s=0x%08x/0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// rem(Calcula o resto da divisão inteira com sinal entre rs1 e rs2.)
			else if (funct3 == 0b110 && funct7 == 0b0000001)
			{
				int32_t rs1_32 = (int32_t)registradores[rs1];
				int32_t rs2_32 = (int32_t)registradores[rs2];
				const uint32_t resultado = (rs2_32 == 0) ? rs1_32 : (rs1_32 == INT32_MIN && rs2_32 == -1) ? 0
																										  : (uint32_t)(rs1_32 % rs2_32);

				fprintf(output, "0x%08x:rem %s,%s,%s %s=0x%08x%%0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// remu(Calcula o resto da divisão inteira sem sinal entre rs1 e rs2)
			else if (funct3 == 0b111 && funct7 == 0b0000001)
			{
				const uint32_t resultado = (registradores[rs2] == 0) ? registradores[rs1] : registradores[rs1] % registradores[rs2];

				fprintf(output, "0x%08x:remu %s,%s,%s %s=0x%08x%%0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}
			// Tratamento da exceção 2 — Illegal Instruction. Quando a instrução não é reconhecida (opcode ou funct inválido)
			else
			{
				// preparando mstatus para a excessão
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc);
				continue;
			}

			break; // acabou as verificações desse caso

			// tipo I-type
		case 0b0010011:
			// addi (soma rs1 com valor imediato e armazena em rd)
			if (funct3 == 0b000)
			{
				const uint32_t resultado = registradores[rs1] + imm_i;

				fprintf(output, "0x%08x:addi %s,%s,0x%03x %s=0x%08x+0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// andi (faz uma operação lógica AND bit a bit entre um registrador e um valor imediato)
			else if (funct3 == 0b111)
			{
				const uint32_t resultado = registradores[rs1] & imm_i;

				fprintf(output, "0x%08x:andi %s,%s,0x%03x %s=0x%08x&0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// ori(Faz um OR bit a bit entre rs1 e um valor imediato (12 bits), armazenando em rd)
			else if (funct3 == 0b110)
			{
				const uint32_t resultado = registradores[rs1] | imm_i;

				fprintf(output, "0x%08x:ori %s,%s,0x%03x %s=0x%08x|0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// xori(Faz um XOR bit a bit entre rs1 e um valor imediato (12 bits), armazenando em rd)
			else if (funct3 == 0b100)
			{
				const uint32_t resultado = registradores[rs1] ^ imm_i;

				fprintf(output, "0x%08x:xori %s,%s,0x%03x %s=0x%08x^0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// slti (Se rs1 for menor que o valor imediato (com sinal), rd=1, senão rd=0)
			else if (funct3 == 0b010)
			{
				const int32_t sinal_rs1 = (int32_t)registradores[rs1]; // Valor de rs1 com sinal
				const uint32_t resultado = (sinal_rs1 < imm_i) ? 1 : 0;

				fprintf(output, "0x%08x:slti %s,%s,0x%03x %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// sltiu (Se rs1 for menor que o valor imediato (sem sinal), rd=1, senão rd=0)
			else if (funct3 == 0b011)
			{
				const uint32_t resultado = (registradores[rs1] < imm_i) ? 1 : 0;

				fprintf(output, "0x%08x:sltiu %s,%s,0x%03x %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						imm_i & 0xFFF,		// imediato do tipo i
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,				// imediato do tipo i
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// slli (Desloca o valor em rs1 para a esquerda, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b001 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] << shamt;

				fprintf(output, "0x%08x:slli %s,%s,0x%02x %s=0x%08x<<0x%02x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						shamt,				// extrai 5 bits
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,				// extrai 5 bits
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// srli (Desloca o valor em rs1 para a direita, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b101 && funct7 == 0b0000000)
			{
				const uint32_t resultado = registradores[rs1] >> shamt;

				fprintf(output, "0x%08x:srli %s,%s,0x%02x %s=0x%08x>>0x%02x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						shamt,				// extrai 5 bits
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,				// extrai 5 bits
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// srai (Desloca o valor em rs1 para a direita, mantendo o bit de sinal (preenche com 0 se positivo, 1 se negativo)
			else if (funct3 == 0b101 && funct7 == 0b0100000)
			{
				const int32_t sinal_rs1 = (int32_t)registradores[rs1]; // Converte para inteiro com sinal
				const uint32_t resultado = sinal_rs1 >> shamt;

				fprintf(output, "0x%08x:srai %s,%s,0x%02x %s=0x%08x>>>0x%02x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						shamt,				// extrai 5 bits
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,				// extrai 5 bits
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}
			// Tratamento da exceção 2 — Illegal Instruction. Quando a instrução não é reconhecida (opcode ou funct inválido)
			else
			{
				// preparando mstatus para a excessão
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc);
				continue;
			}

			break;

		// tipo Load Byte
		case 0b0000011:
			// lb (Carrega um byte da memória ou UART no endereço rs1 + offset, estende para 32 bits e armazena em rd)
			if (funct3 == 0b000)
			{
				const uint32_t endereco = registradores[rs1] + imm_i;
				uint32_t resultado = 0;

				// ACESSO À UART
				if (endereco >= 0x10000000 && endereco <= 0x10000007)
				{
					if (endereco == 0x10000000)
					{
						// UART RHR: lê caractere do terminal UART de entrada
						int c = fgetc(input2);
						if (c == EOF)
						{
							registradoresUART[0] = 0; // nada disponível, retorna 0
						}
						else
						{
							registradoresUART[0] = (uint8_t)c;
						}
						resultado = (uint32_t)(int32_t)((int8_t)registradoresUART[0]);
					}
					else if (endereco == 0x10000005)
					{
						// UART LSR: indica se há dado disponível (bit 0 = 1 se sim)
						int c = fgetc(input2); // tenta ler um caractere
						if (c == EOF)
						{
							registradoresUART[5] = 0x60; // bit 0 = 0 → nada disponível
						}
						else
						{
							ungetc(c, input2);			 // devolve o caractere
							registradoresUART[5] = 0x61; // bit 0 = 1 → dado disponível

							//Ativa a interrupção no PLIC (fonte 1: UART)
		                    plic_pending |= (1 << 1);

						}
						resultado = registradoresUART[5];
					}
					break;
				}

				// ACESSO AO CLINT
				else if (endereco >= 0x02000000 && endereco <= 0x02004004)
				{
					if (endereco == 0x02000000)
					{ // MSIP (Software interrupt pending)
						resultado = clint_msip & 0x1;
					}
					else if (endereco == 0x0200BFF8)
					{ // MTIME (parte baixa dos 64 bits)
						resultado = (uint32_t)(clint_mtime & 0xFFFFFFFF);
					}
					else if (endereco == 0x0200BFFC)
					{ // MTIME (parte alta)
						resultado = (uint32_t)(clint_mtime >> 32);
						printf("teste");
					}
					else if (endereco == 0x02004000)
					{ // MTIMECMP (parte baixa)
						resultado = (uint32_t)(clint_mtimecmp & 0xFFFFFFFF);
					}
					else if (endereco == 0x02004004)
					{ // MTIMECMP (parte alta)
						resultado = (uint32_t)(clint_mtimecmp >> 32);
					}

					break;
				}

				// EXCEÇÃO DE ACESSO INVÁLIDO
				else if (endereco < offset || endereco >= offset + 32 * 1024)
				{
					prepMstatus(&registradoresCSRs[0]);
					registrarExcecao(5, pc, endereco, registradoresCSRs, output, &pc);
					continue;
				}

				// ACESSO NORMAL À MEMÓRIA RAM
				else
				{
					const int8_t byte = (int8_t)mem[endereco - offset];
					resultado = (uint32_t)(int32_t)byte;
				}

				fprintf(output, "0x%08x:lb %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,
						regNomes[rd],
						imm_i & 0xFFF,
						regNomes[rs1],
						regNomes[rd],
						endereco,
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// lh (Carrega um halfword (16 bits) da memória no endereço rs1 + offset (com extensão de sinal)
			else if (funct3 == 0b001)
			{
				const uint32_t endereco = registradores[rs1] + imm_i;

				// Tratamento da exceção 5 — Load Access Fault. Quando a instrução de leitura tenta acessar um endereço inválido na memória
				if (endereco < offset || endereco >= offset + 32 * 1024)
				{
					// preparando mstatus para a excessão
					prepMstatus(&registradoresCSRs[0]);
					registrarExcecao(5, pc, endereco, registradoresCSRs, output, &pc);
					continue;
				}
				// Lê dois bytes (meia palavra) da memória
				uint32_t resultado = 0;

				int16_t halfword = (int16_t)(mem[endereco - offset] | (mem[endereco + 1 - offset] << 8));
				resultado = (uint32_t)(int32_t)halfword;
				fprintf(output, "0x%08x:lh %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,			   // Endereço da instrução
						regNomes[rd],  // Nome do registrador destino
						imm_i & 0xFFF, // imediato do tipo i
						regNomes[rs1], // Nome do registrador rs1
						regNomes[rd],  // Nome do registrador destino
						endereco,	   // endereço usado para encontrar a memoria
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// lw (Carrega uma word (32 bits) da memória no endereço rs1 + offset e armazena em rd)
			else if (funct3 == 0b010)
		    {
			const uint32_t endereco = registradores[rs1] + imm_i;

			// Acesso aos registradores do PLIC (endereços mapeados)
			if (endereco == 0x0C000000) // PRIORITY
			{
				uint32_t resultado = plic_priority;
				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], regNomes[rd], endereco, resultado);
				if (rd != 0)
					registradores[rd] = resultado;
				continue;
			}
			else if (endereco == 0x0C001000) // PENDING
			{
				uint32_t resultado = plic_pending;
				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], regNomes[rd], endereco, resultado);
				if (rd != 0)
					registradores[rd] = resultado;
				continue;
			}
			else if (endereco == 0x0C002000) // ENABLE
			{
				uint32_t resultado = plic_enable;
				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], regNomes[rd], endereco, resultado);
				if (rd != 0)
					registradores[rd] = resultado;
				continue;
			}
			else if (endereco == 0x0C200000) // THRESHOLD
			{
				uint32_t resultado = plic_threshold;
				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], regNomes[rd], endereco, resultado);
				if (rd != 0)
					registradores[rd] = resultado;
				continue;
			}
			else if (endereco == 0x0C200004) // CLAIM
			{
				uint32_t resultado = plic_claim;
				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], regNomes[rd], endereco, resultado);
				if (rd != 0)
					registradores[rd] = resultado;
				continue;
			}

			// ACESSO NORMAL A RAM
			if (endereco < offset || endereco + 3 >= offset + 32 * 1024)
			{
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(5, pc, endereco, registradoresCSRs, output, &pc);
				continue;
			}

			uint32_t resultado = mem[endereco - offset] |
								(mem[endereco + 1 - offset] << 8) |
								(mem[endereco + 2 - offset] << 16) |
								(mem[endereco + 3 - offset] << 24);

			fprintf(output, "0x%08x:lw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
					pc, regNomes[rd], imm_i & 0xFFF, regNomes[rs1], endereco, resultado);

			if (rd != 0)
				registradores[rd] = resultado;

			} 
             //fim lw

			// lbu (Carrega 1 byte da memória no endereço rs1 + offset, faz zero-extend e armazena em rd)
			else if (funct3 == 0b100)
			{

				const uint32_t endereco = registradores[rs1] + imm_i;

				// Tratamento da exceção 5 — Load Access Fault. Quando a instrução de leitura tenta acessar um endereço inválido na memória
				if (endereco < offset || endereco >= offset + 32 * 1024)
				{
					// preparando mstatus para a excessão
					prepMstatus(&registradoresCSRs[0]);
					registrarExcecao(5, pc, endereco, registradoresCSRs, output, &pc);
					continue;
				}
				uint32_t resultado = 0;

				const uint8_t byte = mem[endereco - offset];
				resultado = (uint32_t)byte; // zero-extension

				fprintf(output, "0x%08x:lbu %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,			   // Endereço da instrução
						regNomes[rd],  // Nome do registrador destino
						imm_i & 0xFFF, // imediato do tipo i
						regNomes[rs1], // Nome do registrador rs1
						regNomes[rd],  // Nome do registrador destino
						endereco,	   // endereço usado para encontrar a memoria
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// lhu (Carrega 2 bytes da memória no endereço rs1 + offset, faz zero-extend e armazena em rd)
			else if (funct3 == 0b101)
			{

				const uint32_t endereco = registradores[rs1] + imm_i;

				// Tratamento da exceção 5 — Load Access Fault. Quando a instrução de leitura tenta acessar um endereço inválido na memória
				if (endereco < offset || endereco >= offset + 32 * 1024)
				{
					// preparando mstatus para a excessão
					prepMstatus(&registradoresCSRs[0]);
					registrarExcecao(5, pc, endereco, registradoresCSRs, output, &pc);
					continue;
				}

				const uint16_t halfword = (mem[endereco - offset]) | (mem[endereco + 1 - offset] << 8);
				const uint32_t resultado = (uint32_t)halfword;

				fprintf(output, "0x%08x:lhu %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,			   // Endereço da instrução
						regNomes[rd],  // Nome do registrador destino
						imm_i & 0xFFF, // imediato do tipo i
						regNomes[rs1], // Nome do registrador rs1
						regNomes[rd],  // Nome do registrador destino
						endereco,	   // endereço usado para encontrar a memoria
						resultado);

				if (rd != 0)
				{
					registradores[rd] = resultado;
				}
			}

			// Tratamento da exceção 2 — Illegal Instruction. Quando a instrução não é reconhecida (opcode ou funct inválido)
			else
			{
				// preparando mstatus para a excessão
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc);
				continue;
			}
		
			break;
			

		// tipo Store byte
		case 0b0100011:{
		
			// sb (Armazena 1 byte da parte menos significativa de rs2 na memória [rs1 + offset])
			if (funct3 == 0b000) 
			{
				const uint32_t endereco = registradores[rs1] + imm_s;

				// ACESSO AO CLINT
				if (endereco == 0x02000000)
				{ // MSIP
					clint_msip = registradores[rs2] & 0x1;
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) clint_msip=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], clint_msip);
				}
				else if (endereco == 0x0200BFF8)
				{ // MTIME low
					clint_mtime = (clint_mtime & 0xFFFFFFFF00000000) | (registradores[rs2] & 0xFF);
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) clint_mtime_low+=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], registradores[rs2] & 0xFF);
				}
				else if (endereco == 0x0200BFFC)
				{ // MTIME high
					clint_mtime = (clint_mtime & 0x00000000FFFFFFFF) | ((uint64_t)(registradores[rs2] & 0xFF) << 32);
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) clint_mtime_high+=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], registradores[rs2] & 0xFF);
				}
				else if (endereco == 0x02004000)
				{ // MTIMECMP low
					clint_mtimecmp = (clint_mtimecmp & 0xFFFFFFFF00000000) | (registradores[rs2] & 0xFF);
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) clint_mtimecmp_low=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], registradores[rs2] & 0xFF);
				}
				else if (endereco == 0x02004004)
				{ // MTIMECMP high
					clint_mtimecmp = (clint_mtimecmp & 0x00000000FFFFFFFF) | ((uint64_t)(registradores[rs2] & 0xFF) << 32);
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) clint_mtimecmp_high=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], registradores[rs2] & 0xFF);
				}
				// ACESSO À UART
				else if (endereco == 0x10000000)
				{
					const uint8_t dado = registradores[rs2] & 0xFF;
					fputc(dado, output2);
					fflush(output2);
					registradoresUART[0] = dado;
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) uart[0]=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], dado);
				}
				// ACESSO NORMAL À RAM
				else 
				{
					if (endereco < offset || endereco >= offset + 32 * 1024)
					{
						prepMstatus(&registradoresCSRs[0]);
						registrarExcecao(7, pc, endereco, registradoresCSRs, output, &pc);
						continue;
					}
					const uint8_t resultado = registradores[rs2] & 0xFF;
					mem[endereco - offset] = resultado;
					fprintf(output, "0x%08x:sb %s,0x%03x(%s) mem[0x%08x]=0x%02x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, resultado);
				}
			}

			// sh ( Armazena 2 bytes da parte menos significativa de rs2 na memória [rs1 + offset])
			else if (funct3 == 0b001)
			{
				// Calcula o deslocamento de 12 bits com sinal

				const uint32_t endereco = registradores[rs1] + imm_s;

				// Tratamento da exceção 7 — Store Access Fault. Quando a instrução de escrita tenta acessar um endereço inválido na memória
				if (endereco < offset || endereco + 1 >= offset + 32 * 1024)
				{
					// preparando mstatus para a excessão
					prepMstatus(&registradoresCSRs[0]);
					registrarExcecao(7, pc, endereco, registradoresCSRs, output, &pc);
					continue;
				}
				const uint16_t resultado = (uint16_t)(registradores[rs2] & 0xFFFF); // parte menos significativa de rs2

				mem[endereco - offset] = resultado & 0xFF;
				mem[endereco + 1 - offset] = (resultado >> 8) & 0xFF;

				fprintf(output, "0x%08x:sh %s,0x%03x(%s) mem[0x%08x]=0x%04x\n",
						pc,			   // Endereço da instrução
						regNomes[rs2], // Nome do registrador rs2
						imm_s & 0xFFF, // imedaito do tipo s
						regNomes[rs1], // Nome do registrador rs1
						endereco,	   // endereço
						resultado & 0xFFFF);
			}

			// sw (Armazena 4 bytes da parte menos significativa de rs2 na memória [rs1 + offset])
			else if (funct3 == 0b010)
			{
				const uint32_t endereco = registradores[rs1] + imm_s;

				// ACESSO AO CLINT (sw pode precisar acessar registradores de 32 bits)
				if (endereco == 0x02000000)
				{ // MSIP
					clint_msip = registradores[rs2] & 0x1;
					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, registradores[rs2]);
				}
				else if (endereco == 0x0200BFF8)
				{ // MTIME low (32 bits)
					clint_mtime = (clint_mtime & 0xFFFFFFFF00000000) | registradores[rs2];
					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, registradores[rs2]);
				}
				else if (endereco == 0x0200BFFC)
				{ // MTIME high (32 bits)
					clint_mtime = (clint_mtime & 0x00000000FFFFFFFF) | ((uint64_t)registradores[rs2] << 32);
					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, registradores[rs2]);
				}
				else if (endereco == 0x02004000)
				{ // MTIMECMP low (32 bits)
					clint_mtimecmp = (clint_mtimecmp & 0xFFFFFFFF00000000) | registradores[rs2];
					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, registradores[rs2]);
				}
				else if (endereco == 0x02004004)
				{ // MTIMECMP high (32 bits)
					clint_mtimecmp = (clint_mtimecmp & 0x00000000FFFFFFFF) | ((uint64_t)registradores[rs2] << 32);
					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, registradores[rs2]);
				}
				// ACESSO NORMAL À RAM
				else
				{
					if (endereco < offset || endereco + 3 >= offset + 32 * 1024)
					{
						prepMstatus(&registradoresCSRs[0]);
						registrarExcecao(7, pc, endereco, registradoresCSRs, output, &pc);
						continue;
					}

					const uint32_t resultado = registradores[rs2];
					mem[endereco - offset] = resultado & 0xFF;
					mem[endereco + 1 - offset] = (resultado >> 8) & 0xFF;
					mem[endereco + 2 - offset] = (resultado >> 16) & 0xFF;
					mem[endereco + 3 - offset] = (resultado >> 24) & 0xFF;

					fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
							pc, regNomes[rs2], imm_s & 0xFFF, regNomes[rs1], endereco, resultado);
				}
			}
			// Tratamento da exceção 2 — Illegal Instruction
			else
			{
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc);
				continue;
			}
		
		
			break;
	}
		// tipo Branch
		case 0b1100011:
			// beq (Compara os valores em rs1 e rs2. Se forem iguais, salta para PC + offset)
			if (funct3 == 0b000)
			{

				fprintf(output, "0x%08x:beq %s,%s,0x%03x (0x%08x==0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						(registradores[rs1] == registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] == registradores[rs2])
				{
					pc += imm_b;
					continue;
				}
			}

			// bne (Compara os valores em rs1 e rs2. Se forem diferentes, salta para PC + offset)
			else if (funct3 == 0b001)
			{
				const int condicao = registradores[rs1] != registradores[rs2];

				fprintf(output, "0x%08x:bne %s,%s,0x%03x (0x%08x!=0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						(registradores[rs1] != registradores[rs2]) ? pc + imm_b : pc + 4);

				if (condicao)
				{
					pc += imm_b;
					continue;
				}
			}

			// blt (Compara rs1 e rs2 com sinal. Se rs1 < rs2, salta para PC + offset)
			else if (funct3 == 0b100)
			{
				const int32_t rs1_sinal = registradores[rs1];
				const int32_t rs2_sinal = registradores[rs2];
				// const uint32_t pc_anterior = pc;

				const int condicao = rs1_sinal < rs2_sinal;
				// const uint32_t proximo_pc = condicao ? pc + imm_b : pc + 4;

				// const uint32_t campo_imm_b = (imm_b >> 1) & 0xFFF;

				fprintf(output, "0x%08x:blt %s,%s,0x%03x (0x%08x<0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						((int32_t)registradores[rs1] < (int32_t)registradores[rs2]) ? pc + imm_b : pc + 4);

				if (condicao)
				{
					pc += imm_b;
					continue;
				}
			}

			// bge (Compara rs1 e rs2 com sinal. Se rs1 >= rs2, salta para PC + offset)
			else if (funct3 == 0b101)
			{
				const int32_t rs1_sinal = registradores[rs1];
				const int32_t rs2_sinal = registradores[rs2];

				fprintf(output, "0x%08x:bge %s,%s,0x%03x (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						((int32_t)registradores[rs1] >= (int32_t)registradores[rs2]) ? pc + imm_b : pc + 4);

				if (rs1_sinal >= rs2_sinal)
				{
					pc = pc + imm_b;
					continue;
				}
			}

			// bltu (Compara rs1 e rs2 sem sinal. Se rs1 < rs2, salta para PC + offset)
			else if (funct3 == 0b110)
			{

				fprintf(output, "0x%08x:bltu %s,%s,0x%03x (0x%08x<0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						(registradores[rs1] < registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] < registradores[rs2])
				{
					pc = pc + imm_b;
					continue;
				}
			}

			// bgeu (Compara rs1 e rs2 sem sinal. Se rs1 >= rs2, salta para PC + offset)
			else if (funct3 == 0b111)
			{

				fprintf(output, "0x%08x:bgeu %s,%s,0x%03x (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						imm_b & 0xFFF,		// imediato do tipo b
						registradores[rs1], // valor de rs1
						registradores[rs2], // valor de rs2
						(registradores[rs1] >= registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] >= registradores[rs2])
				{
					pc = pc + imm_b;
					continue;
				}
			}
			// Tratamento da exceção 2 — Illegal Instruction. Quando a instrução não é reconhecida (opcode ou funct inválido)
			else
			{
				// preparando mstatus para a excessão
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc);
				continue;
			}
			break;

		// tipo jump byte
		// jal (Salta para PC + offset e armazena PC + 4 em rd)
		case 0b1101111:
			// if (opcode == 1101111 ){

			// const int32_t offset = (imm_j << 11) >> 11;
			const uint32_t campo_imm_j = (imm_j >> 1) & 0xFFFFF;

			const uint32_t destino = pc + imm_j;
			const uint32_t retorno = pc + 4;

			fprintf(output, "0x%08x:jal %s,0x%05x pc=0x%08x,%s=0x%08x\n",
					pc,			  // Endereço da instrução
					regNomes[rd], // Nome do registrador destino
					campo_imm_j,  // imeadiato do tipo j
					destino,	  // proxima instrução
					regNomes[rd], // Nome do registrador destino
					retorno);

			if (rd != 0)
			{
				registradores[rd] = retorno;
			}

			pc = destino;
			continue; // para não incrementar o PC após salto

			//}

			// tipo jump
			// jalr (Salta para o endereço rs1 + offset e armazena pc + 4 em rd)
		case 0b1100111:
			if (funct3 == 0b000)
			{
				// const int32_t offset = ((int32_t)instrucao) >> 20;
				const uint32_t retorno = pc + 4;
				const uint32_t novo_pc = (registradores[rs1] + imm_i) & ~1;
				fprintf(output, "0x%08x:jalr %s,%s,0x%03x pc=0x%08x+0x%08x,%s=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// nome do rs1
						imm_i & 0xFFF,		// imediato do tipo i
						registradores[rs1], // valor de rs1
						imm_i,				// imediato do tipo i
						regNomes[rd],		// Nome do registrador destino
						retorno);

				if (rd != 0)
				{
					registradores[rd] = retorno;
				}

				pc = novo_pc;
				continue;
			}

			// tipo Upper immediate
			// lui (Carrega um valor imediato de 20 bits nos bits mais altos do registrador, os 12 bits inferiores ficam zerados)
		case 0b0110111:
			const uint32_t resultado_lui = imm_u;

			fprintf(output, "0x%08x:lui %s,0x%05x %s=0x%05x000\n",
					pc,			  // Endereço da instrução
					regNomes[rd], // nome do registrador de destino
					imm_u >> 12,  // imediato do tipo u
					regNomes[rd], // nome do registrador de destino
					imm_u >> 12); // imediato do tipo u

			if (rd != 0)
			{
				registradores[rd] = resultado_lui;
			}

			break;

			// tipo Upper immediate
			// auipc (Carrega um valor imediato de 20 bits nos bits mais altos do registrador, os 12 bits inferiores ficam zerados)
		case 0b0010111:
			const uint32_t resultado_auipc = pc + imm_u;

			fprintf(output, "0x%08x:auipc %s,0x%05x %s=0x%08x+0x%05x000=0x%08x\n",
					pc,			  // Endereço da instrução
					regNomes[rd], // nome do registrador de destino
					imm_u >> 12,  // imediato do tipo u
					regNomes[rd], // nome do registrador de destino
					pc,			  // Endereço da instrução
					imm_u >> 12,  // imediato do tipo u
					resultado_auipc);

			if (rd != 0)
			{
				registradores[rd] = resultado_auipc;
			}
			break;

			// tipo System
			// imediato do tipo system

		case 0b1110011:
			uint32_t imm_csr = (instrucao >> 20) & 0xFFF;
			// ebreak (Interrompe a execução do programa; usada para debug)
			if (funct3 == 0b000 && imm_i == 1)
			{
				fprintf(output, "0x%08x:ebreak\n", pc);
				run = 0;
				continue; // Impede que pc += 4 seja executado
			}

			// Instruções de sistema (CSR + ecall + mret)

			// csrrw (Atualiza CSR com rs1 e salva valor antigo em rd; usada para controle do sistema)
			else if (funct3 == 0b001)
			{ // csrrw
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t rs1_val = registradores[rs1];
				uint32_t valor_antigo = registradoresCSRs[idx];

				if (imm_csr == 0x300)
				{ // mstatus: tratar só MIE e MPIE
					const uint32_t MIE = 1 << 3;
					const uint32_t MPIE = 1 << 7;
					registradoresCSRs[idx] = (registradoresCSRs[idx] & ~(MIE | MPIE)) | (rs1_val & (MIE | MPIE));
				}
				else
				{
					registradoresCSRs[idx] = rs1_val;
				}

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				fprintf(output, "0x%08x:csrrw  %s,%s,%s     %s=%s=0x%08x,%s=%s=0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], regNomes[rs1],
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], regNomes[rs1], rs1_val);
			}

			// csrrs (Lê o CSR, salva em rd e faz OR com rs1; usada para ativar bits)
			else if (funct3 == 0b010)
			{ // csrrs
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t valor_antigo = registradoresCSRs[idx];
				uint32_t rs1_val = registradores[rs1];

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				if (rs1 != 0)
				{
					if (imm_csr == 0x300)
					{ // mstatus
						// OR apenas nos bits sensíveis
						const uint32_t MIE = 1 << 3;
						const uint32_t MPIE = 1 << 7;
						registradoresCSRs[idx] |= (rs1_val & (MIE | MPIE));
					}
					else
					{
						registradoresCSRs[idx] |= rs1_val;
					}
				}

				fprintf(output, "0x%08x:csrrs  %s,%s,%s     %s=%s=0x%08x,%s|=%s=0x%08x|0x%08x=0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], regNomes[rs1],
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], regNomes[rs1],
						valor_antigo, rs1_val, valor_antigo | rs1_val);
			}

			// csrrc (Lê o CSR, salva valor antigo em rd e zera bits indicados por rs1; usada para desativar bits)
			else if (funct3 == 0b011)
			{ // csrrc
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t valor_antigo = registradoresCSRs[idx];
				uint32_t rs1_val = registradores[rs1];

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				if (rs1 != 0)
				{
					if (imm_csr == 0x300)
					{ // mstatus
						// Apenas bits sensíveis (MIE e MPIE)
						const uint32_t MIE = 1 << 3;
						const uint32_t MPIE = 1 << 7;
						registradoresCSRs[idx] &= ~(rs1_val & (MIE | MPIE));
					}
					else
					{
						registradoresCSRs[idx] &= ~rs1_val;
					}
				}

				fprintf(output, "0x%08x:csrrc  %s,%s,%s     %s=%s=0x%08x,%s&=~%s=0x%08x&~0x%08x=0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], regNomes[rs1],
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], regNomes[rs1],
						valor_antigo, rs1_val, valor_antigo & ~rs1_val);
			}

			// csrrwi (Escreve um imediato no CSR e salva valor antigo em rd; usado para controle do sistema)
			else if (funct3 == 0b101)
			{ // csrrwi
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t imm_val = rs1 & 0x1F; // imediato de 5 bits
				uint32_t valor_antigo = registradoresCSRs[idx];

				// Tratamento especial para mstatus
				if (imm_csr == 0x300)
				{
					const uint32_t MIE = 1 << 3;
					const uint32_t MPIE = 1 << 7;

					// Altera apenas MIE e MPIE
					registradoresCSRs[idx] = (valor_antigo & ~(MIE | MPIE)) | (imm_val & (MIE | MPIE));
				}
				else
				{
					registradoresCSRs[idx] = imm_val;
				}

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				fprintf(output, "0x%08x:csrrwi %s,%s,%u     %s=%s=0x%08x,%s=zimm=0x%02x -> 0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], imm_val,
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], imm_val, registradoresCSRs[idx]);
			}

			// csrrsi (Faz OR entre CSR e valor imediato, salvando valor antigo em rd; usada para ativar bits)
			else if (funct3 == 0b110)
			{ // csrrsi
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t imm_val = rs1 & 0x1F; // zimm: imediato no campo rs1
				uint32_t valor_antigo = registradoresCSRs[idx];

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				if (imm_val != 0)
				{
					if (imm_csr == 0x300)
					{ // mstatus
						const uint32_t MIE = 1 << 3;
						const uint32_t MPIE = 1 << 7;
						registradoresCSRs[idx] |= (imm_val & (MIE | MPIE));
					}
					else
					{
						registradoresCSRs[idx] |= imm_val;
					}
				}

				fprintf(output, "0x%08x:csrrsi %s,%s,%u     %s=%s=0x%08x,%s|=0x%02x=0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], imm_val,
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], imm_val, registradoresCSRs[idx]);
			}

			// csrrci (Desativa bits do CSR com imediato e salva valor antigo em rd; usada para controle do sistema)
			else if (funct3 == 0b111)
			{ // csrrci
				int idx = csrIndex(imm_csr);
				if (idx == -1)
				{
					fprintf(stderr, "CSR 0x%03x não suportado.\n", imm_csr);
					run = 0; // ou continue;
					break;	 // depende da estrutura do seu código
				}
				uint32_t valor_antigo = registradoresCSRs[idx];
				uint32_t imm_val = rs1 & 0x1F; // zimm

				if (rd != 0)
				{
					registradores[rd] = valor_antigo;
				}

				if (imm_val != 0)
				{
					if (imm_csr == 0x300)
					{ // mstatus
						const uint32_t MIE = 1 << 3;
						const uint32_t MPIE = 1 << 7;
						registradoresCSRs[idx] &= ~(imm_val & (MIE | MPIE));
					}
					else
					{
						registradoresCSRs[idx] &= ~imm_val;
					}
				}

				fprintf(output, "0x%08x:csrrci %s,%s,%u     %s=%s=0x%08x,%s&=~0x%02x=0x%08x\n",
						pc,
						regNomes[rd], regNomesCSRs[idx], imm_val,
						regNomes[rd], regNomesCSRs[idx], valor_antigo,
						regNomesCSRs[idx], imm_val, registradoresCSRs[idx]);
			}

			// ecall (Solicita serviço ao sistema; gera uma exceção para tratar chamada de ambiente)
			else if (funct3 == 0b000 && imm_i == 0)
			{
				fprintf(output, "0x%08x:ecall\n", pc);
				// preparando mstatus para a excessão
				prepMstatus(&registradoresCSRs[0]);
				registrarExcecao(11, pc, instrucao, registradoresCSRs, output, &pc); // 11 = código de exceção para ECALL

				continue; // Pula o pc += 4 no final do loop
			}

			// mret (Retorna do modo de exceção para o ponto onde o programa foi interrompido)
			else if (funct3 == 0b000 && imm_i == 0x302)
			{
				int idx_mepc = csrIndex(833);	 // mepc
				int idx_mstatus = csrIndex(768); // mstatus

				uint32_t mepc = registradoresCSRs[idx_mepc];
				uint32_t mstatus = registradoresCSRs[idx_mstatus];

				// Atualiza mstatus:
				// MIE ← MPIE (bit 3 ← bit 7)
				mstatus = (mstatus & ~(1 << 3)) | (((mstatus >> 7) & 1) << 3);

				// MPIE ← 1
				mstatus |= (1 << 7);

				// MPP ← 00 (bits 12-11)
				mstatus &= ~(3 << 11);

				registradoresCSRs[idx_mstatus] = mstatus;

				fprintf(output, "0x%08x:mret       pc=mepc=0x%08x\n", pc, mepc);

				pc = mepc;
				continue;
			}

			break;

			// Tratamento da exceção 2 — Illegal Instruction. Quando a instrução não é reconhecida (opcode ou funct inválido)
		default:
			// fprintf(output, "Instrução inválida em 0x%08x: 0x%08x (opcode: 0x%02x)\n", // para achar o erro
			// pc, instrucao, opcode);
			// preparando mstatus para a excessão
			prepMstatus(&registradoresCSRs[0]);
			registrarExcecao(2, pc, instrucao, registradoresCSRs, output, &pc); // código 2 = Illegal Instruction
			continue;															// Isso será tratado pelo handler
		}
			pc += 4;

			// Incremento do tempo do CLINT (mtime)
			clint_mtime++;

			// VERIFICAÇÃO DA INTERRUPÇÃO POR TIMER
			if ((registradoresCSRs[1] & (1 << 7)) && // mie: habilita interrupção de timer
				(registradoresCSRs[0] & (1 << 3)) && // mstatus: interrupções globais habilitadas
				(clint_mtime >= clint_mtimecmp))	 // mtime atingiu mtimecmp
			{
				// Prepara os CSRs para a interrupção
				registradoresCSRs[4] = 0x80000007; // mcause (bit 31 = 1 indica interrupção, código 7 = timer)
				registradoresCSRs[3] = pc + 4;	   // mepc = próxima instrução
				registradoresCSRs[5] = 0x00000000; // mtval = zero em interrupções

				prepMstatus(&registradoresCSRs[0]);

				fprintf(output, ">interrupt:timer                   cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
						registradoresCSRs[4], registradoresCSRs[3], registradoresCSRs[5]);

				// Redireciona o PC para mtvec
				pc = (registradoresCSRs[2] & ~0x3) + 4 * (registradoresCSRs[4] & 0x7FFFFFFF);

				continue;
			}

			// VERIFICAÇÃO DE INTERRUPÇÃO DE SOFTWARE
			if ((registradoresCSRs[1] & 0x8) && // mie: software interrupt enable (bit 3)
				(registradoresCSRs[0] & 0x8) && // mstatus: global interrupt enable (bit 3)
				(clint_msip & 0x1))				// msip: interrupção de software solicitada
			{
				registradoresCSRs[4] = 0x80000003; // mcause: software interrupt
				registradoresCSRs[3] = pc + 4;	   // mepc: próxima instrução
				registradoresCSRs[5] = 0;		   // mtval: valor adicional (0 para interrupções)

				prepMstatus(&registradoresCSRs[0]);

				fprintf(output, ">interrupt:software                cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
						registradoresCSRs[4], registradoresCSRs[3], registradoresCSRs[5]);

				// Redireciona o PC para mtvec
				pc = (registradoresCSRs[2] & ~0x3) + 4 * (registradoresCSRs[4] & 0x7FFFFFFF);
				continue;
			}

			// VERIFICAÇÃO DE INTERRUPÇÃO EXTERNA (PLIC – UART)
			if ((registradoresCSRs[1] & (1 << 11)) &&    // mie: interrupção externa habilitada (bit 11)
				(registradoresCSRs[0] & (1 << 3)) &&     // mstatus: global interrupt enable
				(plic_enable & (1 << 1)) &&              // PLIC: UART habilitada
				(plic_pending & (1 << 1)))               // PLIC: UART sinalizou interrupção
			{
				registradoresCSRs[4] = 0x8000000B; // mcause: 11 = External Interrupt (bit 31 = 1)
				registradoresCSRs[3] = pc + 4;     // mepc: próxima instrução
				registradoresCSRs[5] = 0;          // mtval: 0 para interrupções

				prepMstatus(&registradoresCSRs[0]);

				fprintf(output, ">interrupt:external                cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
						registradoresCSRs[4], registradoresCSRs[3], registradoresCSRs[5]);

				// Limpa o pending (interrupção foi "reconhecida")
				plic_pending &= ~(1 << 1);
				
				// Redireciona o PC para mtvec como nas outras interrupções
				pc = (registradoresCSRs[2] & ~0x3) + 4 * (registradoresCSRs[4] & 0x7FFFFFFF);
				continue;
			}

		}
		return 0;
	}