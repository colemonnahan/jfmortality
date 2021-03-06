/// Un TMB modelo de tipo Cormack-Jolly-Seber de seccion 14.5 of
/// Korner-Nievergelt et al 2015. Actualizado para TMB y maximo
/// verosimiltud. Cole Monnahan, 7/2018.

#include <TMB.hpp>
// Function for detecting NAs
template<class Type>
bool isNA(Type x){
  return R_IsNA(asDouble(x));
}

// logit funcion
template<class Type>
Type inv_logit(Type x){
  Type y= 1/(1+exp(-x));
  return(y);
}

template<class Type>
Type objective_function<Type>::operator() ()
{
  DATA_INTEGER(I);              // numero de los individuos
  DATA_INTEGER(K);              // numero de los periodos
  DATA_MATRIX(CH);		// la historia de las capturas (0/1) [IxK]
  DATA_IVECTOR(last);		// el ultimo periodo visto
  DATA_MATRIX(counts);		// numeros de recapturas (individos x periodos)
  DATA_VECTOR(effort);		// la esfuerza por cada periodo
  DATA_VECTOR(lengths);		// longitudes de los individuos
  DATA_IVECTOR(first);		// el primero periodo
  DATA_FACTOR(sexo);		// sexo de los individuos
  DATA_FACTOR(evento)		// evento de las marcas
  DATA_VECTOR(lengths_pred);	// para calcular selectividad
  DATA_VECTOR(esfuerzo_pred); // para calcular catchability
  
  // efectos fijos
  PARAMETER_VECTOR(logNatM);	// mortalidad de naturleza (H y M)
  PARAMETER(logr);		// effecto de los numeros de capturas
  //  PARAMETER(logk);		// effecto de esfuerza de probabilidad de captura
  PARAMETER(a); 		// efecto de la selectividad
  PARAMETER(b); 		// efecto de la selectividad
  //  PARAMETER_ARRAY(tau);		// efectos aleatorios por k por machos
  PARAMETER_VECTOR(tau); 
  // PARAMETER_MATRIX(mu_tau);	// los promedios de los efectos aleatorios
  PARAMETER(mu_tau);
  PARAMETER(logsigma_tau);	// la varianza de todos los efectos aleatorios
  PARAMETER(theta); 		// la maxima probabilidad de captura
  PARAMETER(logM_mu);
  PARAMETER(logM_sigma);
  Type nll=0.0;			// negativa log verosimiltud
  matrix<Type> p(I,K); 		// probabilidad de las capturas
  matrix<Type> phi(I,K);	// prob. de la sobrevivencia
  matrix<Type> chi(I,K+1);	// prob. nunca de ver despues un periodo

  p.setZero();
  phi.setZero();
  chi.setZero();

  int kk;
  vector<Type> NatM(K);
  vector<Type> pcap(K);
  NatM=exp(logNatM);
  Type max_prob=inv_logit(theta);
  Type r=exp(logr);
  Type sigma_tau=exp(logsigma_tau);
  // la primero columna es por machos y la segunda por hembras
  array<Type> k(K,2,3); // periodos por generos por eventos
  k.setZero();
  for(int t=0;t<K;t++) { // periodos
    // for(int ev=0; ev<3; ev++){ // eventos
    // // non-centered efectos aleatorios
    //   k(t,0,ev)=exp(tau(t,0,ev)*sigma_tau+mu_tau(0,ev)); // machos
    //   k(t,1,ev)=exp(tau(t,1,ev)*sigma_tau+mu_tau(1,ev)); // hembras
    // }
    pcap(t)=inv_logit(tau(t));
  }

  // TMB usa indices de 0, no de 1, entoces tenemos que estar cuidadoso, y
  // uso un "-1" para ser claro que pasa.
  for(int i=0; i<I; i++){ // iterando sobre cada individuos
    // inicializacion estan vivo en periodo uno
    //    phi(i,first(i)-1)=exp(-NatM(sexo(i))-counts(i,first(i)-1)*r);
    phi(i,first(i)-1)=exp(-NatM(0)-counts(i,first(i)-1)*r);
    p(i,first(i)-1)=1;
    for(int t=first(i); t<K; t++) {
      // calcular prob. capturas como una funcion de efectos y covariables
      // p(i,t) =max_prob*(1-exp(-k(t,sexo(i),evento(i))*effort(t)))/(1+exp(-a*(lengths(i)-b)));
      p(i,t)=pcap(t);
      // calcular prob. sobrevivencia como una funcion de efectos y
      // covariables
      phi(i,t) = exp(-NatM(t)-counts(i,t-1)*r);
    }
    // la probabilidad de no ser visto nunca mas, usa un indice reverso
    // para calcular hacia atras usando recursion.
    chi(i,K+1-1) = 1.0;
    kk = K;
    while (kk > last(i)) {
      chi(i,kk-1) = (1 - phi(i,kk-1-1)) + phi(i,kk-1-1) * (1 - p(i,kk-1)) * chi(i,kk+1-1);
      kk = kk - 1;
    }
    chi(i,1-1) = (1 - p(i,1-1)) * chi(i,2-1);
  }

  //// calcular la verosimiltud
  // los datos
  for(int i=0; i<I; i++){ 
    // probabilidad de sobrevivencia, que es conocido porque first<k<last
    for (int t=first(i); t<last(i); t++) {
    	nll-= log(phi(i,t-1));
    }
    // probabilidad de captura, dado viva (como CH[i,t]~bernoulli(p[i,t]);)
    for(int t=0; t< last(i); t++){
      // NA significa que no hubo esfuerzo en este periodo
      if(!isNA(CH(i,t))){
	if(CH(i,t)>=1){
	  nll-= log(p(i,t));
	} else {
	  nll-= log(1-p(i,t));
	}
      }
    }
    // probabilidad de no ser capturado despues el proximo periodo fue visto 
    nll-= log(chi(i,last(i)+1-1));
  }

  // // Probabilidad de los efectos aleatorios
  for(int t=0;t<K;t++) { // periodos
    // for(int ev=0; ev<3; ev++){ // eventos
    //   nll-=dnorm(tau(t,0,ev), Type(0), Type(1), true);
    //   nll-=dnorm(tau(t,1,ev), Type(0), Type(1), true);
    // }
    nll-=dnorm(logNatM(t), logM_mu, logM_sigma, true);
    nll-=dnorm(tau(t), mu_tau, sigma_tau, true);
  }
  
  // vector<Type> pcap_pred(lengths_pred.size());
  // for(int i=0; i<pcap_pred.size(); i++){
  //   // The probability of capture as function of length given maximum effort 
  //   pcap_pred(i)=1;//max_prob*(1-exp(-k(0,0,0)*Type(3.5)))/(1+exp(-a*(lengths_pred(i)-b)));
  //   //      1/(1+exp(-a*(lengths_pred(i)-b)));
  // }
  // vector<Type> catchabilityM_pred(esfuerzo_pred.size());
  // vector<Type> catchabilityH_pred(esfuerzo_pred.size());
  // for(int i=0; i<esfuerzo_pred.size(); i++){
  //   // dado el promedio de k
  //   catchabilityM_pred(i)=1-exp(-exp(mu_tauM)*esfuerzo_pred(i));
  //   catchabilityH_pred(i)=1-exp(-exp(mu_tauH)*esfuerzo_pred(i));
  // }
  
  // // reportando
  // vector<Type> kM1(K);
  // vector<Type> kM2(K);
  // vector<Type> kM3(K);  
  // vector<Type> kH1(K);
  // vector<Type> kH2(K);
  // vector<Type> kH3(K);  
  // kM1=k.col(0).col(0);
  // kM2=k.col(1).col(0);
  // kM3=k.col(2).col(0);
  // kH1=k.col(0).col(1);
  // kH2=k.col(1).col(1);
  // kH3=k.col(2).col(1);
  // ADREPORT(kM1);
  // ADREPORT(kM2);
  // ADREPORT(kM3);
  // ADREPORT(kH1);
  // ADREPORT(kH2);
  // ADREPORT(kH3);
  // ADREPORT(r);

  // Type NatMortH=NatM(0);
  // Type NatMortM=NatM(1);
  // ADREPORT(NatMortH);
  // ADREPORT(NatMortM);
  // ADREPORT(a);
  // ADREPORT(b);
  vector<Type> surv= exp(-NatM);
  ADREPORT(surv);
  ADREPORT(NatM);
  ADREPORT(logNatM);
  // ADREPORT(pcap_pred);
  ADREPORT(max_prob);
  Type NatM_mu=exp(logM_mu);
  ADREPORT(NatM_mu);
  ADREPORT(pcap);
  ADREPORT(tau);
  // ADREPORT(catchabilityM_pred);
  // ADREPORT(catchabilityH_pred);
  REPORT(p);
  // REPORT(phi);
  // REPORT(CH);
  return(nll);
}
// final del archivo
