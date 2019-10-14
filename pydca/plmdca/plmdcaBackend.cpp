#include "include/plmdca.h"

/*Implements the pseudolikelihood maximization direct couplings analysis 
for protein and RNA sequences.

Author: Mehari B. Zerihun

*/

extern "C" float* plmdcaBackend(unsigned short const biomolecule, 
    unsigned short const num_site_states, 
    const char* msa_file, unsigned int const seqs_len, 
    float const seqid, float const lambda_h, 
    float const lambda_J, unsigned int const max_iteration, 
    const unsigned int num_threads, bool verbose )
{  
    /*Interface for the Python implementation of plmDCA. 

    Parameters
    ----------
        biomolecule     : Type of biomolecule (protein or RNA).
        num_site_states : Number of states/residues plus gap for MSA data.
        msa_file        : Path to the FASTA formatted MSA file.
        seqs_len        : The length of sequences in MSA data.
        seqid           : Sequence identity threshold.
        lambda_h        : Regularization parameter for fields.
        lambda_J        : Regularization parameter for couplings.
        max_iteration   : Maximum number of gradient decent iterations.
        num_threads     : Number of threads for PlmDCA (when OpenMP is supported).
        verbose         : Print logging message on the terminal.

    Returns
    -------
        h_and_J        : Fields and couplings array. This data is feteched into the
            Python interface. 

    */
   #if defined(_OPENMP)
    // can use multiple threads
    #else 
        if(num_threads > 1){
            std::cerr << "Cannot set multiple threads when OpenMP is not supported\n";
            throw std::runtime_error("Invalid number of threads");
        }
    #endif

    const int total_num_params = seqs_len * num_site_states + seqs_len * (seqs_len - 1) * num_site_states * num_site_states/2 ; 
    static PlmDCA plmdca_inst(msa_file, biomolecule, seqs_len, num_site_states, seqid, lambda_h, lambda_J, num_threads);
    static int num_iterations = max_iteration;
    static auto logging = verbose;
    
    class ObjectiveFunction{
        /*Objective Function for lbfgs input. 
        
        Attributes
        ----------
            m_x     : A dynamic array containing fields and couplings.
        */
        protected:
            float* m_x;
        
        public:
            ObjectiveFunction() : m_x(NULL) {}        
            float* getFieldsAndCouplings() {return m_x; }

            int run(int N)
            {
                /*Performs plmDCA computation using LBFGS optimization.

                Parameters
                ----------
                    N       : Total number of fields and couplings. 

                Returns
                -------
                    ret     : Exit status of LBFGS optimization.
                */
        
                float fx;
                m_x = lbfgs_malloc(N);
                if (m_x == NULL) {
                printf("ERROR: Failed to allocate a memory block for variables.\n");
                return 1;
                }
                //initialize parameters
                lbfgs_parameter_t param;
                lbfgs_parameter_init(&param);
                param.epsilon = 1E-3;
                param.max_iterations = num_iterations;
                param.max_linesearch = 5;
                param.ftol = 1E-4;
                //param.wolfe = 0.2;
                param.m = 5 ;

                plmdca_inst.initFieldsAndCouplings(m_x);
                //Start the L-BFGS optimization; this will invoke the callback functions
                //evaluate() and progress() when necessary.
                        
                int ret = lbfgs(N, m_x, &fx, _evaluate, _progress, this, &param);

                /* Report the result. */
                if(logging){
                    fprintf(stderr, "L-BFGS optimization terminated with status code = %d\n", ret);
                    fprintf(stderr, "fx = %f\n", fx);
                }
        
                return ret;
            }


        protected:
            static float _evaluate( void* instance, const float*x, float* g, const int n, const float step)
            {
                /*Computes the gradient of the regularized negative pseudolikelihood function for 
                protein/RNA alignments. 

                Parameters
                ----------
                    instance    : An instance of ObjectiveFunction class. 
                    x           : Array of fields and couplings.
                    g           : Array of gradient of the negative log pseudolikelihood.
                        of the conditional probablity for protein/RNA alignments.
                    n           : Number of fields and couplings?
                    step        : The step size for gradient decent.

                Returns
                --------
                    fx          : Value of plmDCA objective function

                */

                return reinterpret_cast<ObjectiveFunction*>(instance)->evaluate(x, g, n, step);
            }


            float evaluate(const float*x, float* g, const int n, const float step)
            {
                float fx;
                fx = plmdca_inst.gradient(x, g);
                return fx;
            }


            static int _progress(void* instance, const float* x, const float* g, const float fx, 
                const float xnorm, const float gnorm, const float step, int n, int k, int ls)
            {
                return reinterpret_cast<ObjectiveFunction*>(instance)->progress(x, g, fx, xnorm, gnorm, step, n, k, ls);
            }


            int progress(const float* x, const float* g, const float fx, const float xnorm, const float gnorm,
                const float step, int n, int k, int ls)
            {
                if(logging){
                    fprintf(stderr, "Iteration %d:\n", k);
                    fprintf(stderr, "fx = %f xnorm = %f, gnorm = %f, step = %f\n", fx, xnorm, gnorm, step);
                    fprintf(stderr, "\n");
                }
                return 0;
            }
    };
    
    
    // Start computation 
    ObjectiveFunction fun;
    const int N = total_num_params;

    fun.run(N);
    auto h_and_J = fun.getFieldsAndCouplings();

    return h_and_J;
}


extern "C" void freeFieldsAndCouplings(void * h_and_J)
{  
    /*  Frees memory that has been used to store fields and couplings before 
        they are captured in the Python interface. 
        Parameter h_and_J must be passed using ctypes.byref from Python.

        Parameters
        ----------
            h_and_J : Pointer to the fields and couplings vector 
        
        Returns
        -------
            void    : No return value

    */
   float* h_and_J_casted = static_cast<float*>(h_and_J);  
    if(h_and_J_casted !=nullptr){
        delete [] h_and_J_casted;
        h_and_J_casted = nullptr;
    }
}
    