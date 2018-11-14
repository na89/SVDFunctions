ReplaceMissing <- function(x){
	k <- which(is.na(x), arr.ind=T)
  if (length(k) > 0){
	  x[k] <- round(rowMeans(x, na.rm=T)[k[,1]])
  }
	x
}

EstimateCountsGLM <- function(gmatrixToCount){
  counts <- mat.or.vec(nr = nrow(gmatrixToCount), nc = 3)
  for (i in 1:nrow(gmatrixToCount)){
    AlleleCounts <- table(gmatrixToCount[i,])
    if("0" %in% names(AlleleCounts)){
      homref <- AlleleCounts["0"]
    } else {
	    homref <- 0
    }
    if("1" %in% names(AlleleCounts)){
	    het <- AlleleCounts["1"]
    } else {
	    het <- 0
    }
     if("2" %in% names(AlleleCounts)){
	    homalt <- AlleleCounts["2"]
    } else {
	    homalt <- 0
    }
    counts[i, 1] <- homref
    counts[i, 2] <- het
    counts[i, 3] <- homalt
  }
  counts
}

#' Create two files needed to run SCORE platform
#'
#' This function generates matrix of left singular 
#' vectors and summary genotype counts. Binary PLINK files are used as an input.
#' @param bfile  name of the PLINK files (i.e. if you have "example.bed", 
#' "example.bim", "example.fam" files as PLINK binary data storage, 
#' bfile="example")
#' @param ref file with a list of DNA variant locations and corresponding 
#' reference alleles. Could be downloaded from http://dnascore.net -> Tutorial
#' @import magrittr
#' @export
BED2GMatrix <- function(bfile, ref) {
  ref <- normalizePath(ref)
  ref <- data.table::fread(ref, header = T)
  ref <- as.data.frame(ref)
  regions.storage <- utils::read.table(paste(bfile, ".bim", sep = ""))
  regions <- regions.storage[,2] %>% unlist %>% as.character
  
  outfilename <- bfile
  output <- paste(outfilename, "_gmatrix.txt", sep="")
  message(date()," Starting genotype matrix conversion...")
  
  PlinkData <- snpStats::read.plink(bed = normalizePath(paste0(bfile, ".bed")),
                                    bim = normalizePath(paste0(bfile, ".bim")),
                                    fam = normalizePath(paste0(bfile, ".fam")), 
                                    select.snps = regions)
  
  gmatrix <- methods::as(PlinkData$genotypes,'numeric') %>% t(.)
  meta <- PlinkData$map
  meta[,1] <- paste(paste("chr", meta[,1], sep=""), meta[,4], sep=":")
  
  ref_subset <- ref[ref[,1] %in% meta[,1], 2] %>% unlist %>% as.character
  if (any(meta[[5]] %>% as.character != ref_subset)){
    mismatch <- which(meta[,5] %>% unlist %>% as.character != ref_subset)
  }
   
  rownames(meta)<-c()
  meta <- meta[,c(1, 5, 6)]
  meta[mismatch,c(2,3)] <- meta[mismatch,c(3,2)]
  for(i in mismatch){
     geno.vector<-as.numeric(as.character(unlist(gmatrix[i,])))
     geno.vector[which(gmatrix[i,] == 0)] <- 2
     geno.vector[which(gmatrix[i,] == 2)] <- 0
     gmatrix[i,] <- geno.vector
  }
  gmatrix <- cbind(meta, gmatrix)
  
  utils::write.table(gmatrix, output, quote=F, sep="\t", row.names=F)
  
  message(date(), " Generating Sharable Data")
  case_counts <- cbind(gmatrix[,c(1,2,3)],
                       EstimateCountsGLM(as.matrix(gmatrix[,-c(1, 2, 3)])))
  
  colnames(case_counts) <- c("VAR","REF","ALT","REFHOM","HET","ALTHOM")
  gmatrix <- ReplaceMissing(as.matrix(gmatrix[,-c(1,2,3)]))
  u <- svd(gmatrix,nu=min(c(10,abs(ncol(gmatrix)-1),abs(nrow(gmatrix)-1))))$u
  write.table(u, paste(outfilename, "U.txt", sep="_"), row.names=F, 
              col.names = F, sep = "\t", quote=F)
  write.table(case_counts, paste(outfilename, "_case_counts.txt", sep=""), 
              sep="\t", quote = F, row.names = F)
  message(date(), " Success. Exiting...")
}
