#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{ // argumento para abrir o projeto no terminal, entrega a entrada e fala a saida
	// "./meuprograma" "entrada.hex"  "saida.out"

	FILE *input = fopen(argv[1], "r");	// abre um arquivo de entrada
	FILE *output = fopen(argv[2], "w"); // abre/cria em arquivo de saida (os arquivos do argumento do main)

	// FILE *input = fopen("input.hex", "r");
	// FILE *output = fopen("output.out", "w");

	// offset é o ponto de partida da memória simulada
	const uint32_t offset = 0x80000000; // Vamos fingir que a memória do processador começa no endereço 0x80000000. offset significa deslocamento
	uint32_t registradores[32] = {0};	// 32 registradores inicializados com 0
	// abreviações do RISC-V para os registradores0
	const char *regNomes[32] = {"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

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

	// teste manual do professor para preencher a memória com instruções conhecidas, sem depender da leitura de um arquivo
	//.
	//.
	//.
	// inicio do simulador de instruções
	uint8_t run = 1; // pra controlar o loop
	// laço principal de execução do simulador
	while (run)
	{ // o loop que vai buscar e decodificar as instruções

		// converte mem para um tipo de 4 bytes, acessa a posição correta da instrução dividindo por 4 para acessar apenas 1  instrução inteira  por indice
		// uint32_t instrucao = ((uint32_t*)mem)[(pc - offset)>>2];
		uint32_t instrucao = ((uint32_t *)(mem))[(pc - offset) >> 2];

		if (pc < offset || pc >= offset + 32 * 1024)
		{
			printf("PC fora do intervalo da memória: 0x%08x\n", pc);
			run = 0;
			break; // ou run = 0;
		}

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
			if (funct3 == 0b000 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] + registradores[rs2];
				fprintf(output, "0x%08x:add %s,%s,%s %s=0x%08x+0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				// Atualizando o registrador de destino, se não for registradores[0]
				if (rd != 0){ // Ela atualiza o registrador destino rd com o valor do cálculo (data), mas só se o registrador rd não for o registrador x0.
					registradores[rd] = resultado;
				}
			}
			// sub (subtração)
			else if (funct7 == 0b0100000 && funct3 == 0b000){
				const uint32_t resultado = registradores[rs1] - registradores[rs2];
				fprintf(output, "0x%08x:sub %s,%s,%s %s=0x%08x-0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// sll (desloca o conteúdo de rs1 logicamente para a esquerda)
			else if (funct3 == 0b001 && funct7 == 0b0000000){
				const uint8_t deslocar = registradores[rs2] & 0b11111;	   // filtra os 5 bits menos significativos
				const uint32_t resultado = registradores[rs1] << deslocar; // desloca os 5 bits a esquerda

				fprintf(output, "0x%08x:sll %s,%s,%s %s=0x%08x<<u5=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// slt (Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2 na comparação com sinal)
			else if (funct3 == 0b010 && funct7 == 0b0000000){ 
				int32_t sinal_rs1 = (int32_t)registradores[rs1];
				int32_t sinal_rs2 = (int32_t)registradores[rs2];
				const uint32_t resultado = (sinal_rs1 < sinal_rs2) ? 1 : 0; // Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2

				fprintf(output, "0x%08x:slt %s,%s,%s %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// sltu (Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2 na comparação sem sinal)
			else if (funct3 == 0b011 && funct7 == 0b0000000){
				const uint32_t resultado = (registradores[rs1] < registradores[rs2]) ? 1 : 0; // Define o registrador rd como 1 se o valor em rs1 for menor que o valor em rs2

				fprintf(output, "0x%08x:sltu %s,%s,%s %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// xor( operação bit a bit (lógica) de OU EXCLUSIVO entre os valores dos registradores rs1 e rs2)
			else if (funct3 == 0b100 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] ^ registradores[rs2];

				fprintf(output, "0x%08x:xor %s,%s,%s %s=0x%08x^0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}
			// srl(Desloca os bits do valor em rs1 para a direita, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b101 && funct7 == 0b0000000){
				const uint8_t deslocar = registradores[rs2] & 0b11111;	   // filtra os 5 bits menos significativos
				const uint32_t resultado = registradores[rs1] >> deslocar; // desloca os 5 bits a direita

				fprintf(output, "0x%08x:srl %s,%s,%s %s=0x%08x>>u5=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// sra(Desloca os bits de rs1 para a direita, mantendo o bit de sinal (preenche com 0 se positivo, 1 se negativo)
			else if (funct3 == 0b101 && funct7 == 0b0100000){
				const uint8_t deslocar = registradores[rs2] & 0b11111; // filtra os 5 bits menos significativos
				const int32_t Sinal_rs1 = (int32_t)registradores[rs1];
				const uint32_t resultado = (uint32_t)(Sinal_rs1 >> deslocar);

				fprintf(output, "0x%08x:sra %s,%s,%s %s=0x%08x>>>u5=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// or(Ela realiza um OU bit a bit (bitwise OR) entre os valores contidos nos registradores rs1 e rs2)
			else if (funct3 == 0b110 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] | registradores[rs2];

				fprintf(output, "0x%08x:or %s,%s,%s %s=0x%08x|0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// and (compara os bits de dois registradores (rs1 e rs2) e retorna 1 somente se ambos os bits forem 1, caso contrário, retorna 0)
			else if (funct3 == 0b111 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] & registradores[rs2];

				fprintf(output, "0x%08x:and %s,%s,%s %s=0x%08x&0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);


				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// mul(executa uma multiplicação entre os valores inteiros contidos nos registradores rs1 e rs2)
			else if (funct3 == 0b000 && funct7 == 0b0000001){
				const uint32_t resultado = registradores[rs1] * registradores[rs2];

				fprintf(output, "0x%08x:mul %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);


				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// mulh(guarda os 32 bits mais significativos da multiplicação com sinal)
			else if (funct3 == 0b001 && funct7 == 0b0000001){
				int64_t rs1_64 = (int64_t)(int32_t)registradores[rs1];
				int64_t rs2_64 = (int64_t)(int32_t)registradores[rs2];
				int64_t produto = rs1_64 * rs2_64;
				const uint32_t resultado = (uint32_t)(produto >> 32); //sem sinal

				fprintf(output, "0x%08x:mulh %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,                        // Endereço da instrução
						regNomes[rd],             // Nome do registrador destino
						regNomes[rs1],            // Nome do registrador rs1
						regNomes[rs2],           // Nome do registrador rs2
						regNomes[rd],            // Nome de novo para mostrar atribuição
						registradores[rs1],      // Valor de rs1
						registradores[rs2],      // Valor de rs2
						resultado);


				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// mulhsu (guarda os 32 bits mais significativos da multiplicação sem sinal)
			else if (funct3 == 0b010 && funct7 == 0b0000001){
				int64_t rs1_64 = (int64_t)(int32_t)registradores[rs1]; // rs1 com sinal
				uint64_t rs2_64 = (uint32_t)registradores[rs2];		   // rs2 sem sinal
				int64_t produto = rs1_64 * rs2_64;					   // resultado 64 bits
				const uint32_t resultado = (uint32_t)(produto >> 32);  // parte alta

				fprintf(output, "0x%08x:mulhsu %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,                           //endereço da instrução 
						regNomes[rd],                 //nome do registrador de destino 
						regNomes[rs1],                //nome do registrador rs1
						regNomes[rs2],                //nome do registrador rs2
						regNomes[rd],                 // Nome de novo para mostrar atribuição
						registradores[rs1],           // Valor de rs1
						registradores[rs2],           // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// mulhu (Multiplica os valores não assinados de rs1 e rs2 (32 bits cada))
			else if (funct3 == 0b011 && funct7 == 0b0000001){
				uint64_t rs1_64 = (uint32_t)registradores[rs1];
				uint64_t rs2_64 = (uint32_t)registradores[rs2];
				uint64_t produto = rs1_64 * rs2_64;
				const uint32_t resultado = (uint32_t)(produto >> 32);

				fprintf(output, "0x%08x:mulhu %s,%s,%s %s=0x%08x*0x%08x=0x%08x\n",
						pc,                           //endereço da instrução 
						regNomes[rd],                 //nome do registrador de destino 
						regNomes[rs1],                //nome do registrador rs1
						regNomes[rs2],                //nome do registrador rs2
						regNomes[rd],                 // Nome de novo para mostrar atribuição
						registradores[rs1],           // Valor de rs1
						registradores[rs2],           // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// div( faz a divisão com sinal)
			else if (funct3 == 0b100 && funct7 == 0b0000001){
				int32_t rs1_32 = (int32_t)registradores[rs1];
				int32_t rs2_32 = (int32_t)registradores[rs2];

				const uint32_t resultado = (rs2_32 == 0) ? 0xFFFFFFFF : (rs1_32 == INT32_MIN && rs2_32 == -1) ? (uint32_t)INT32_MIN : (uint32_t)(rs1_32 / rs2_32);

				fprintf(output, "0x%08x:div %s,%s,%s %s=0x%08x/0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// divu( faz a divisão sem sinal)
			else if (funct3 == 0b101 && funct7 == 0b0000001){
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

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// rem(Calcula o resto da divisão inteira com sinal entre rs1 e rs2.)
			else if (funct3 == 0b110 && funct7 == 0b0000001){
				int32_t rs1_32 = (int32_t)registradores[rs1];
				int32_t rs2_32 = (int32_t)registradores[rs2];
				const uint32_t resultado = (rs2_32 == 0) ? rs1_32 : (rs1_32 == INT32_MIN && rs2_32 == -1) ? 0  : (uint32_t)(rs1_32 % rs2_32);

				fprintf(output, "0x%08x:rem %s,%s,%s %s=0x%08x%%0x%08x=0x%08x\n",
						pc,					// Endereço da instrução
						regNomes[rd],		// Nome do registrador destino
						regNomes[rs1],		// Nome do registrador rs1
						regNomes[rs2],		// Nome do registrador rs2
						regNomes[rd],		// Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						registradores[rs2], // Valor de rs2
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// remu(Calcula o resto da divisão inteira sem sinal entre rs1 e rs2)
			else if (funct3 == 0b111 && funct7 == 0b0000001){
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

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}
			break; // acabou as verificações desse caso

			// tipo I-type
		case 0b0010011:
			// addi (soma rs1 com valor imediato e armazena em rd)
			if (funct3 == 0b000){
				const uint32_t resultado = registradores[rs1] + imm_i;

				fprintf(output, "0x%08x:addi %s,%s,0x%03x %s=0x%08x+0x%08x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,              //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// andi (faz uma operação lógica AND bit a bit entre um registrador e um valor imediato)
			else if (funct3 == 0b111){
				const uint32_t resultado = registradores[rs1] & imm_i;

				fprintf(output, "0x%08x:andi %s,%s,0x%03x %s=0x%08x&0x%08x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,      //imediato do tipo i
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,              //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// ori(Faz um OR bit a bit entre rs1 e um valor imediato (12 bits), armazenando em rd)
			else if (funct3 == 0b110){
				const uint32_t resultado = registradores[rs1] | imm_i;

				fprintf(output, "0x%08x:ori %s,%s,0x%03x %s=0x%08x|0x%08x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,              //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// xori(Faz um XOR bit a bit entre rs1 e um valor imediato (12 bits), armazenando em rd)
			else if (funct3 == 0b100){
				const uint32_t resultado = registradores[rs1] ^ imm_i;

				fprintf(output, "0x%08x:xori %s,%s,0x%03x %s=0x%08x^0x%08x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						imm_i,             //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// slti (Se rs1 for menor que o valor imediato (com sinal), rd=1, senão rd=0)
			else if (funct3 == 0b010){
				const int32_t sinal_rs1 = (int32_t)registradores[rs1]; // Valor de rs1 com sinal
				const uint32_t resultado = (sinal_rs1 < imm_i) ? 1 : 0;

				fprintf(output, "0x%08x:slti %s,%s,0x%03x %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,        //imediato do tipo i 
						regNomes[rd],         // Nome de novo para mostrar atribuição
						registradores[rs1],  // Valor de rs1
						imm_i,               //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// sltiu (Se rs1 for menor que o valor imediato (sem sinal), rd=1, senão rd=0)
			else if (funct3 == 0b011){
				const uint32_t resultado = (registradores[rs1] < imm_i) ? 1 : 0;

				fprintf(output, "0x%08x:sltiu %s,%s,0x%03x %s=(0x%08x<0x%08x)=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						imm_i & 0xFFF,     //imediato do tipo i 
						regNomes[rd],         // Nome de novo para mostrar atribuição
						registradores[rs1],  // Valor de rs1
						imm_i,             //imediato do tipo i 
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// slli (Desloca o valor em rs1 para a esquerda, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b001 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] << shamt;

				fprintf(output, "0x%08x:slli %s,%s,0x%02x %s=0x%08x<<0x%02x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						shamt,               // extrai 5 bits
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,              // extrai 5 bits
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// srli (Desloca o valor em rs1 para a direita, preenchendo os bits vazios com zeros)
			else if (funct3 == 0b101 && funct7 == 0b0000000){
				const uint32_t resultado = registradores[rs1] >> shamt;

				fprintf(output, "0x%08x:srli %s,%s,0x%02x %s=0x%08x>>0x%02x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						shamt,              //extrai 5 bits
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,              //extrai 5 bits
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// srai (Desloca o valor em rs1 para a direita, mantendo o bit de sinal (preenche com 0 se positivo, 1 se negativo)
			else if (funct3 == 0b101 && funct7 == 0b0100000){
				const int32_t sinal_rs1 = (int32_t)registradores[rs1]; // Converte para inteiro com sinal
				const uint32_t resultado = sinal_rs1 >> shamt;

				fprintf(output, "0x%08x:srai %s,%s,0x%02x %s=0x%08x>>>0x%02x=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						regNomes[rs1],      // Nome do registrador rs1
						shamt,              //extrai 5 bits
						regNomes[rd],       // Nome de novo para mostrar atribuição
						registradores[rs1], // Valor de rs1
						shamt,              //extrai 5 bits
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}
			break;

		// tipo Load Byte
		case 0b0000011:
			// lb (Carrega um byte (8 bits) da memória no endereço rs1 + offset (com extensão de sinal), estende o sinal para 32 bits e armazena em rd)
			if (funct3 == 0b000){
				const uint32_t endereco = registradores[rs1] + imm_i;
				// Implementação correta do load (assumindo que 'memoria' é seu array de bytes)
				uint32_t resultado = 0;
				const int8_t byte = (int8_t)mem[endereco - offset];
				resultado = (uint32_t)(int32_t)byte;

				fprintf(output, "0x%08x:lb %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rd],       // Nome do registrador destino
						endereco,          //endereço usado para encontrar a memoria    
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// lh (Carrega um halfword (16 bits) da memória no endereço rs1 + offset (com extensão de sinal)
			else if (funct3 == 0b001){
				const uint32_t endereco = registradores[rs1] + imm_i;
				// Lê dois bytes (meia palavra) da memória
				uint32_t resultado = 0;

				int16_t halfword = (int16_t)(mem[endereco - offset] | (mem[endereco + 1 - offset] << 8));
				resultado = (uint32_t)(int32_t)halfword;
				fprintf(output, "0x%08x:lh %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rd],       // Nome do registrador destino
						endereco,          //endereço usado para encontrar a memoria    
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// lw (Carrega uma word (32 bits) da memória no endereço rs1 + offset e armazena em rd)
			else if (funct3 == 0b010){
				const uint32_t endereco = registradores[rs1] + imm_i;
				// Lê quatro bytes (palavra completa)
				uint32_t resultado = 0;
				resultado = mem[endereco - offset] | (mem[endereco + 1 - offset] << 8) | (mem[endereco + 2 - offset] << 16) | (mem[endereco + 3 - offset] << 24);

				fprintf(output, "0x%08x:lw %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rd],       // Nome do registrador destino
						endereco,          //endereço usado para encontrar a memoria    
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// lbu (Carrega 1 byte da memória no endereço rs1 + offset, faz zero-extend e armazena em rd)
			else if (funct3 == 0b100){

				const uint32_t endereco = registradores[rs1] + imm_i;
				uint32_t resultado = 0;

				const uint8_t byte = mem[endereco - offset];
				resultado = (uint32_t)byte; // zero-extension

				fprintf(output, "0x%08x:lbu %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rd],       // Nome do registrador destino
						endereco,          //endereço usado para encontrar a memoria    
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}

			// lhu (Carrega 2 bytes da memória no endereço rs1 + offset, faz zero-extend e armazena em rd)
			else if (funct3 == 0b101){

				const uint32_t endereco = registradores[rs1] + imm_i;
				const uint16_t halfword = (mem[endereco - offset]) | (mem[endereco + 1 - offset] << 8);
				const uint32_t resultado = (uint32_t)halfword;

				fprintf(output, "0x%08x:lhu %s,0x%03x(%s) %s=mem[0x%08x]=0x%08x\n",
						pc,                 // Endereço da instrução
						regNomes[rd],       // Nome do registrador destino
						imm_i & 0xFFF,      //imediato do tipo i 
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rd],       // Nome do registrador destino
						endereco,          //endereço usado para encontrar a memoria    
						resultado);

				if (rd != 0){
					registradores[rd] = resultado;
				}
			}
			break;

		// tipo Store byte
		case 0b0100011:
			// sb (armazena 1 byte (8 bits) da palavra armazenada no registrador rs2 na memória, no endereço calculado por rs1 + offset)
			if (funct3 == 0b000){
				// Monta o offset tipo S (12 bits com sinal)
				// Extensão de sinal manual de 12 bits
				const uint32_t endereco = registradores[rs1] + imm_s;
				const uint8_t resultado = registradores[rs2] & 0xFF; // Pega só o byte menos significativo

				mem[endereco - offset] = resultado;

				fprintf(output, "0x%08x:sb %s,0x%03x(%s) mem[0x%08x]=0x%02x\n",
						pc,                // Endereço da instrução
						regNomes[rs2],     // Nome do registrador rs2
						imm_s & 0xFFF,      //imediato do tipo s
						regNomes[rs1],     // Nome do registrador rs2
						endereco,           //endereço 
						resultado & 0xFF);   

				//  if (rd != 0){
				// registradores[rd] = resultado;
				// }
			}

			// sh ( Armazena 2 bytes da parte menos significativa de rs2 na memória [rs1 + offset])
			else if (funct3 == 0b001){
				// Calcula o deslocamento de 12 bits com sinal

				const uint32_t endereco = registradores[rs1] + imm_s;

				const uint16_t resultado = (uint16_t)(registradores[rs2] & 0xFFFF); // parte menos significativa de rs2

				mem[endereco - offset] = resultado & 0xFF;
				mem[endereco + 1 - offset] = (resultado >> 8) & 0xFF;

				fprintf(output, "0x%08x:sh %s,0x%03x(%s) mem[0x%08x]=0x%04x\n",
						pc,              // Endereço da instrução
						regNomes[rs2],   // Nome do registrador rs2
						imm_s & 0xFFF,    //imedaito do tipo s
						regNomes[rs1],     // Nome do registrador rs1
						endereco,           //endereço
						resultado & 0xFFFF);

				// if (rd != 0){
				// registradores[rd] = resultado;
				// }
			}

			// sw ( Armazena 2 bytes da parte menos significativa de rs2 na memória [rs1 + offset])
			else if (funct3 == 0b010){

				const uint32_t endereco = registradores[rs1] + imm_s;

				const uint32_t resultado = registradores[rs2];
				// Armazena os 4 bytes na memória simulada (assumindo 'mem' como array de bytes)

				mem[endereco - offset] = resultado & 0xFF;
				mem[endereco + 1 - offset] = (resultado >> 8) & 0xFF;
				mem[endereco + 2 - offset] = (resultado >> 16) & 0xFF;
				mem[endereco + 3 - offset] = (resultado >> 24) & 0xFF;

				fprintf(output, "0x%08x:sw %s,0x%03x(%s) mem[0x%08x]=0x%08x\n",
						pc,              // Endereço da instrução
						regNomes[rs2],   // Nome do registrador rs2
						imm_s & 0xFFF,   //imediato do tipo s
						regNomes[rs1],   // Nome do registrador rs1
						endereco,        //endereço 
						resultado);

				// if (rd != 0){
				// registradores[rd] = resultado;
				// }
			}

			break;

		// tipo Branch
		case 0b1100011:
			// beq (Compara os valores em rs1 e rs2. Se forem iguais, salta para PC + offset)
			if (funct3 == 0b000){

				fprintf(output, "0x%08x:beq %s,%s,0x%03x (0x%08x==0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						(registradores[rs1] == registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] == registradores[rs2]){
					pc += imm_b;
					continue;
				}
			}

			// bne (Compara os valores em rs1 e rs2. Se forem diferentes, salta para PC + offset)
			else if (funct3 == 0b001){
				const int condicao = registradores[rs1] != registradores[rs2];

				fprintf(output, "0x%08x:bne %s,%s,0x%03x (0x%08x!=0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						(registradores[rs1] != registradores[rs2]) ? pc + imm_b : pc + 4);

				if (condicao){
					pc += imm_b;
					continue;
				}
			}

			// blt (Compara rs1 e rs2 com sinal. Se rs1 < rs2, salta para PC + offset)
			else if (funct3 == 0b100){
				const int32_t rs1_sinal = registradores[rs1];
				const int32_t rs2_sinal = registradores[rs2];
				//const uint32_t pc_anterior = pc;

				const int condicao = rs1_sinal < rs2_sinal;
				//const uint32_t proximo_pc = condicao ? pc + imm_b : pc + 4;

				//const uint32_t campo_imm_b = (imm_b >> 1) & 0xFFF;

				fprintf(output, "0x%08x:blt %s,%s,0x%03x (0x%08x<0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						((int32_t)registradores[rs1] < (int32_t)registradores[rs2]) ? pc + imm_b : pc + 4);

				if (condicao){
					pc += imm_b;
					continue;
				}
			}

			// bge (Compara rs1 e rs2 com sinal. Se rs1 >= rs2, salta para PC + offset)
			else if (funct3 == 0b101){
				const int32_t rs1_sinal = registradores[rs1];
				const int32_t rs2_sinal = registradores[rs2];

				fprintf(output, "0x%08x:bge %s,%s,0x%03x (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						((int32_t)registradores[rs1] >= (int32_t)registradores[rs2]) ? pc + imm_b : pc + 4);

				if (rs1_sinal >= rs2_sinal){
					pc = pc + imm_b;
					continue;
				}
			}

			// bltu (Compara rs1 e rs2 sem sinal. Se rs1 < rs2, salta para PC + offset)
			else if (funct3 == 0b110){

				fprintf(output, "0x%08x:bltu %s,%s,0x%03x (0x%08x<0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						(registradores[rs1] < registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] < registradores[rs2]){
					pc = pc + imm_b;
					continue;
				}
			}

			// bgeu (Compara rs1 e rs2 sem sinal. Se rs1 >= rs2, salta para PC + offset)
			else if (funct3 == 0b111){

				fprintf(output, "0x%08x:bgeu %s,%s,0x%03x (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rs1],     // Nome do registrador rs1
						regNomes[rs2],    // Nome do registrador rs2
						imm_b & 0xFFF,    //imediato do tipo b
						registradores[rs1], //valor de rs1
						registradores[rs2],  //valor de rs2
						(registradores[rs1] >= registradores[rs2]) ? pc + imm_b : pc + 4);

				if (registradores[rs1] >= registradores[rs2]){
					pc = pc + imm_b;
					continue;
				}
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
					pc,               // Endereço da instrução
					regNomes[rd],     // Nome do registrador destino
					campo_imm_j,     //imeadiato do tipo j 
					destino,         //proxima instrução 
					regNomes[rd],    // Nome do registrador destino
					retorno);

			if (rd != 0){
				registradores[rd] = retorno;
			}

			pc = destino;
			continue; // para não incrementar o PC após salto

			//}

			// tipo jump
			// jalr (Salta para o endereço rs1 + offset e armazena pc + 4 em rd)
		case 0b1100111:
			if (funct3 == 0b000){
				// const int32_t offset = ((int32_t)instrucao) >> 20;
				const uint32_t retorno = pc + 4;
				const uint32_t novo_pc = (registradores[rs1] + imm_i) & ~1;
				fprintf(output, "0x%08x:jalr %s,%s,0x%03x pc=0x%08x+0x%08x,%s=0x%08x\n",
						pc,                // Endereço da instrução
						regNomes[rd],      // Nome do registrador destino
						regNomes[rs1],     //nome do rs1
						imm_i & 0xFFF,     //imediato do tipo i 
						registradores[rs1], //valor de rs1
						imm_i,             //imediato do tipo i 
						regNomes[rd],      // Nome do registrador destino
						retorno);

				if (rd != 0){
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
					pc,            // Endereço da instrução
					regNomes[rd],  // nome do registrador de destino
					imm_u >> 12,  //imediato do tipo u 
					regNomes[rd], // nome do registrador de destino 
					imm_u >> 12); //imediato do tipo u 

			if (rd != 0){
				registradores[rd] = resultado_lui;
			}

			break;
			

			// tipo Upper immediate
			// auipc (Carrega um valor imediato de 20 bits nos bits mais altos do registrador, os 12 bits inferiores ficam zerados)
		case 0b0010111:
			// if (opcode == 0010111){
			const uint32_t resultado_auipc = pc + imm_u;

			fprintf(output, "0x%08x:auipc %s,0x%05x %s=0x%08x+0x%05x000=0x%08x\n",
					pc,              // Endereço da instrução
					regNomes[rd],    // nome do registrador de destino
					imm_u >> 12,     //imediato do tipo u 
					regNomes[rd],      // nome do registrador de destino
					pc,                // Endereço da instrução
					imm_u >> 12,     //imediato do tipo u 
					resultado_auipc);

			if (rd != 0){
				registradores[rd] = resultado_auipc;
			}
			//}

			break;

			// tipo System
		case 0b1110011:
			// ebreak (Interrompe a execução do programa; usada para debug)
			if (funct3 == 0b000 && imm_i == 1){
				fprintf(output, "0x%08x:ebreak\n", pc);
				run = 0;
				continue; // Impede que pc += 4 seja executado
			}
			break;

		default:
			fprintf(output, "Instrução inválida em 0x%08x: 0x%08x (opcode: 0x%02x)\n", // para achar o erro 
					pc, instrucao, opcode);
			run = 0;
			break;
		}
		pc += 4; // incrementando + 4 para o pc apontar para a proxima instrução
	}
	return 0;
}
