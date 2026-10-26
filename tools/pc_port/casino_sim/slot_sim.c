/* Monte Carlo of the Slot Machine's own code (tools/pc_port/casino_econ.py builds and runs it).
 *
 * slot_machine_sim_src.c is src/slot_machine.c with two changes made by the builder: the payout table is writable, and each
 * payout of a line counts that line (gLines). Everything else is the game's: the machine's bias draws (DrawMachineBias),
 * where each reel stops once the player presses A (ReelTask_DecideStop with the game's stopping routines), the ReelTime
 * draw (GetReelTimeDraw, ShouldReelTimeMachineExplode), the lines a bet plays and what they match (CheckMatch), what a Replay
 * or a Big Bonus does to the machine (SlotTask_CheckMatches, copied). The player is a press at a random moment: each reel's
 * offset is a random multiple of its speed when A is pressed. Prints one STAT line per machine (the six the Game Corner
 * has) with the lines won per match and the segments of each kind, read by casino_econ.py.
 */
#include "slot_machine_sim_src.c"
extern int printf(const char *, ...);
extern void *calloc(unsigned, unsigned);
extern void free(void *);
extern long atol(const char *);
extern int atoi(const char *);
extern double sqrt(double);

struct Task gTasks[NUM_TASKS];
double gHist7[6];                       // spins by the number of lines of 7s they paid (never more than one)
struct Sprite gSprites[MAX_SPRITES + 1];


static void RandomizeReel(int reel)
{
    // the reel turns REEL_NORMAL_SPEED pixels a frame from a symbol edge: its offset is always a multiple of that
    sSlotMachine->reelPixelOffsets[reel] = (Random() % (REEL_HEIGHT / REEL_NORMAL_SPEED)) * REEL_NORMAL_SPEED;
    AdvanceSlotReel(reel, 0);
}

static void StopReel(int reel)
{
    struct Task t;
    memset(&t, 0, sizeof(t));
    t.data[15] = reel; // tReelId
    RandomizeReel(reel);
    ReelTask_DecideStop(&t);
    // ReelTask_MoveToStop, a frame at a time: to the next symbol edge, then each extra turn
    while (sSlotMachine->reelPixelOffsets[reel] % REEL_SYMBOL_HEIGHT != 0)
        AdvanceSlotReelToNextSymbol(reel, REEL_NORMAL_SPEED);
    while (sSlotMachine->reelExtraTurns[reel] > 0)
    {
        sSlotMachine->reelExtraTurns[reel]--;
        do
            AdvanceSlotReel(reel, REEL_NORMAL_SPEED);
        while (sSlotMachine->reelPixelOffsets[reel] % REEL_SYMBOL_HEIGHT != 0);
    }
}

struct SimStat
{
    double spins, paid, bet, free;
    double matchCount[9];      // number of lines matched, by MATCH_*
    double matchPay[9];
    double rt_events, rt_draws[6], rt_explode;
    double replays;
    double segLoss, segWin, segReplayOnly, segFirstWin;
    double lines[9];
    double payHist[16];
};

static struct SimStat st;

// One paid spin (and the free replays it may cause); returns the payout of the whole chain.
static long RunSpin(int bet, long *paidBet)
{
    long chain = 0;
    int first = 1;
    sSlotMachine->bet = bet;
    sSlotMachine->payout = 0;
    for (;;)
    {
        int i;
        sSlotMachine->currentReel = LEFT_REEL;
        DrawMachineBias();
        if (sSlotMachine->machineBias & BIAS_REELTIME)
        {
            // ReelTime_Init: reelTimeSpinsLeft = 0, then GetReelTimeDraw
            sSlotMachine->reelTimeSpinsLeft = 0;
            GetReelTimeDraw();
            st.rt_events++;
            if (sSlotMachine->reelTimeDraw == 0)
            {
                int check;
                // the checks: explode on a hit for checks 0..3, otherwise it lands on 0 after the 4th (checks > 3)
                for (check = 0; check <= 3; check++)
                {
                    if (ShouldReelTimeMachineExplode(check))
                    {
                        st.rt_explode++;
                        break;
                    }
                }
                st.rt_draws[0]++;
            }
            else
            {
                st.rt_draws[sSlotMachine->reelTimeDraw]++;
                if (sSlotMachine->pikaPowerBolts)
                    sSlotMachine->pikaPowerBolts = 0;
                sSlotMachine->reelTimeSpinsUsed = 0;
                sSlotMachine->reelTimeSpinsLeft = sSlotMachine->reelTimeDraw;
            }
            sSlotMachine->machineBias &= ~BIAS_REELTIME;
        }
        ResetBiasFailure();
        for (i = 0; i < NUM_REELS; i++)
            StopReel(i);
        // SlotTask_CheckMatches
        sSlotMachine->machineBias &= (BIAS_STRAIGHT_7 | BIAS_MIXED_7);
        { double before7 = gLines[6]+gLines[7]+gLines[8], before = 0; int kk; for (kk = 0; kk < 9; kk++) before += gLines[kk]; (void)before;
        CheckMatch();
        { int n7 = (int)(gLines[6]+gLines[7]+gLines[8]-before7); if (n7 > 5) n7 = 5; gHist7[n7]++; } }
        if (sSlotMachine->reelTimeSpinsLeft)
        {
            sSlotMachine->reelTimeSpinsLeft--;
            sSlotMachine->reelTimeSpinsUsed++;
        }
        st.spins++;
        if (sSlotMachine->payout > 0) { st.segWin++; if (first) st.segFirstWin++; }
        else if (sSlotMachine->matches) st.segReplayOnly++;
        else st.segLoss++;
        if (sSlotMachine->matches)
        {
            chain += sSlotMachine->payout;
            st.paid += sSlotMachine->payout;
            for (i = 0; i < 9; i++)
                if (sSlotMachine->matches & (1 << i))
                    st.matchCount[i]++;
            if ((sSlotMachine->netCoinLoss -= sSlotMachine->payout) < 0)
                sSlotMachine->netCoinLoss = 0;
            if (sSlotMachine->matches & ((1 << MATCH_MIXED_7) | (1 << MATCH_BLUE_7) | (1 << MATCH_RED_7)))
            {
                sSlotMachine->machineBias &= ~(BIAS_STRAIGHT_7 | BIAS_MIXED_7);
                if (sSlotMachine->matches & ((1 << MATCH_BLUE_7) | (1 << MATCH_RED_7)))
                {
                    sSlotMachine->reelTimeSpinsLeft = 0;
                    sSlotMachine->reelTimeSpinsUsed = 0;
                    sSlotMachine->luckyGame = FALSE;
                    if (sSlotMachine->matches & (1 << MATCH_BLUE_7))
                        sSlotMachine->luckyGame = TRUE;
                }
            }
            if ((sSlotMachine->matches & (1 << MATCH_POWER)) && sSlotMachine->pikaPowerBolts < 16)
                sSlotMachine->pikaPowerBolts++;
        }
        else if ((sSlotMachine->netCoinLoss += sSlotMachine->bet) > 9999)
            sSlotMachine->netCoinLoss = 9999;
        if (first)
        {
            *paidBet = bet;
            first = 0;
        }
        // a replay respins at once, bet kept and not charged again
        if (sSlotMachine->matches & (1 << MATCH_REPLAY))
        {
            st.replays++;
            sSlotMachine->payout = 0;
            continue;
        }
        break;
    }
    return chain;
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 1000000;
    int bet = argc > 2 ? atoi(argv[2]) : 3;
    long m, i;
    int k;
    int luckyMode = argc > 4 ? atoi(argv[4]) : 2;      /* 0 an unlucky game, 1 a lucky one, 2 each visit rolls it (the game's own) */
    SeedRng(argc > 3 ? atoi(argv[3]) : 12345);
    for (m = 0; m < NUM_SLOT_MACHINE_IDS; m++)
    {
        double paidM = 0, betM = 0;
        memset(&st, 0, sizeof(st));
        for (k = 0; k < 16; k++) gLines[k] = 0;
        sSlotMachine = calloc(1, sizeof(*sSlotMachine));
        sSlotMachine->machineId = (int)m;
        for (i = 0; i < n; i++)
        {
            long b, p;
            if (i % 400 == 0)
            {
                sSlotMachine->luckyGame = luckyMode == 2 ? (Random() & 1) : luckyMode;
                sSlotMachine->pikaPowerBolts = 0;
                sSlotMachine->machineBias = 0;
                sSlotMachine->reelTimeSpinsLeft = 0;
                sSlotMachine->netCoinLoss = 0;
                for (k = 0; k < NUM_REELS; k++)
                {
                    sSlotMachine->reelPositions[k] = sInitialReelPositions[k][sSlotMachine->luckyGame] % SYMBOLS_PER_REEL;
                    sSlotMachine->reelPixelOffsets[k] = (REEL_HEIGHT - sSlotMachine->reelPositions[k] * REEL_SYMBOL_HEIGHT) % REEL_HEIGHT;
                }
            }
            p = RunSpin(bet, &b);
            paidM += p; betM += b;
        }
        printf("STAT bet=%d machine=%ld paid_spins=%ld segs_loss=%.0f segs_win=%.0f segs_replayonly=%.0f rt_events=%.0f rt_explode=%.0f lines=", bet, m, n, st.segLoss, st.segWin, st.segReplayOnly, st.rt_events, st.rt_explode);
        for (k = 0; k < 9; k++) printf("%.0f%s", gLines[k], k < 8 ? "," : "\n");
        printf("HIST7 bet=%d machine=%ld %.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n", bet, m, gHist7[0],gHist7[1],gHist7[2],gHist7[3],gHist7[4],gHist7[5]);
        for (k = 0; k < 6; k++) gHist7[k] = 0;
        free(sSlotMachine);
    }
    return 0;
}
