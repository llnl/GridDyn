function v = griddyn_query_load_failure()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 6);
  end
  v = vInitialized;
end
